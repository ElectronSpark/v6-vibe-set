#!/usr/bin/env python3
"""Offline regression checks for Chromium span, receive and evidence handling."""
import importlib.util
import gzip
import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from chromium_trace_common import REPO, read_json, write_json, resource_limits,load_trace
import chromium_trace_common as common


def load_module(name):
    spec = importlib.util.spec_from_file_location(name, pathlib.Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


inventory = load_module('chromium-trace-inventory')
mojo = load_module('chromium-mojo-boundary')
frame = load_module('chromium-frame-analysis')
compactor = load_module('chromium-trace-compact')
sharder = load_module('chromium-trace-shard')


def event(name, ts, dur=1, pid=1, tid=2, ph='X', **fields):
    return dict(name=name, ts=ts, dur=dur, pid=pid, tid=tid, ph=ph,
                cat='media', args={}, **fields)


def receive(method, ts, dur=1, iface='CompositorFrameSinkClient', **fields):
    row = next(r for r in mojo.TABLE if r['interface_tag'].endswith('.' + iface) and r['method'] == method)
    result = event('Receive mojo message', ts, dur, **fields)
    result.update(cat='toplevel,mojom', args={'chrome_mojo_event_info':{
        'ipc_hash':row['ipc_hash'], 'mojo_interface_tag':row['interface_tag']}})
    return result


class TraceTests(unittest.TestCase):
    def test_strict_stack_thread_isolation_and_boundary(self):
        rows = [event('VideoOuter', 1, ph='B'), event('VideoOther', 1, tid=3, ph='B'),
                event('VideoInner', 2, ph='B'), event('', 3, ph='E'),
                event('', 4, ph='E'), event('', 5, ph='E')]
        alignment = dict(validated=True, method='synthetic', evidence='unit test',
                         trace_start_us=2, trace_end_us=3, uncertainty_us=0)
        timeline = {}
        result = inventory.analyze(rows, {}, alignment, timeline)
        self.assertEqual(result['pairing']['unmatched_end_count'], 1)
        self.assertEqual(result['pairing']['pending_begin_count'], 1)
        self.assertEqual(result['pairing']['closed_boundary_crossing_count'], 1)
        self.assertEqual([(r['name'],r['end']-r['ts']) for r in timeline['spans']],
                         [('VideoInner',1),('VideoOuter',3)])

    def test_local_async_ids_and_flow_not_duration(self):
        rows = [event('VideoAsync',1,ph='b',id2={'local':'x'}),
                event('VideoAsync',2,ph='b',pid=9,id2={'local':'x'}),
                event('VideoAsync',3,ph='e',id2={'local':'x'}),
                event('VideoFlow',4,ph='s',id=1), event('VideoFlow',5,ph='f',id=1)]
        timeline = {}
        result = inventory.analyze(rows, {}, None, timeline)
        self.assertEqual(result['pairing']['pending_begin_count'],1)
        self.assertEqual(len(timeline['spans']),1)
        self.assertEqual(timeline['spans'][0]['pid'],'1')

    def test_named_mismatch_is_not_guessed(self):
        result = inventory.analyze([event('VideoA',1,ph='B'), event('VideoB',2,ph='E')], {}, None)
        self.assertEqual(result['pairing']['malformed_count'],1)
        self.assertEqual(result['pairing']['pending_begin_count'],1)
        self.assertEqual(result['pairing']['unmatched_end_count'],1)

    def test_unvalidated_alignment_rejected(self):
        with self.assertRaises(ValueError):
            inventory.analyze([], {}, dict(trace_start_us=1, trace_end_us=2))
        with self.assertRaises(ValueError):
            mojo.analyze([], dict(trace_start_us=1, trace_end_us=2))

    def test_multiple_video_tracks_rejected(self):
        timeline = {'spans':[event('VideoFrameCompositor::SetCurrentFrame',1),
                             event('VideoFrameCompositor::SetCurrentFrame',2,pid=9)], 'instants':[]}
        with self.assertRaisesRegex(ValueError,'exactly one video selection thread'):
            frame.analyze_trial('test',{},timeline,{'complete':{'payload':{'samples':[]}}},{})


class MojoTests(unittest.TestCase):
    def test_original_checks(self):
        self.assertTrue(mojo.self_test()['passed'])

    def test_response_and_unrelated_names_excluded(self):
        for name in ('Receive mojo reply', 'Receive reply viz::mojom::CompositorFrameSinkClient::DidReceiveCompositorFrameAck',
                     'unrelated scope'):
            e = receive('DidReceiveCompositorFrameAck',1)
            e['name'] = name
            self.assertIsNone(mojo.identify_receive(e))
        e['name'] = 'Receive viz::mojom::CompositorFrameSinkClient::DidReceiveCompositorFrameAck'
        self.assertIsNotNone(mojo.identify_receive(e))

    def test_reentrant_endpoints_and_playback_alignment(self):
        rows = [receive('DidReceiveCompositorFrameAck',0,100),
                receive('DidReceiveCompositorFrameAck',10,5), event(mojo.SUBMIT,110)]
        alignment = dict(validated=True, method='synthetic', evidence='unit test',
                         trace_start_us=105,trace_end_us=120,uncertainty_us=0)
        report = mojo.analyze(rows, alignment)['video_threads'][0]
        self.assertEqual(report['attempts'][0]['previous_direct_ack']['end_us'],100)
        self.assertEqual(report['playback_window']['direct_ack_receives'],0)
        self.assertEqual(report['playback_window']['submission_attempts'],1)

    def test_boundary_and_overlapping_constructions_unknown(self):
        for app in (event(mojo.APPEND,20,0), event(mojo.APPEND,10,10)):
            report = mojo.analyze([event(mojo.SUBMIT,10,10),app])['video_threads'][0]
            self.assertIsNone(report['attempts'][0]['frame_constructed'])
            self.assertEqual(report['constructions'],0)
        report = mojo.analyze([event(mojo.SUBMIT,0,20),event(mojo.SUBMIT,5,10),
                               event(mojo.APPEND,6,1)])['video_threads'][0]
        self.assertEqual(report['ambiguous_construction_attempts'],2)

    def test_malformed_scopes_and_bundle_payload_no_ack_credit(self):
        bundle = receive('FlushNotifications',0,20,iface='FrameSinkBundleClient')
        bundle['args']['chrome_mojo_event_info'].update(payload_size=999,data_num_bytes=1024)
        rows = [bundle,event(mojo.SUBMIT,10),event(mojo.SUBMIT,20,-1),
                event(mojo.SUBMIT,float('nan')),None]
        report = mojo.analyze(rows)
        self.assertEqual(report['malformed_event_count'],3)
        self.assertIsNone(report['pending_ack_count'])
        self.assertFalse(report['video_threads'][0]['bundle_ack_contents_observed'])
        self.assertEqual(report['video_threads'][0]['direct_ack_receive_count'],0)


class EvidenceTests(unittest.TestCase):
    def test_input_guards_and_no_overwrite(self):
        with tempfile.TemporaryDirectory(prefix='.chromium-analysis-test-',dir=REPO) as temp:
            root = pathlib.Path(temp)
            good = root / 'input.json'
            identity = write_json(good, {'value':1})
            value, second = read_json(good)
            self.assertEqual(value,{'value':1})
            self.assertEqual(identity,second)
            with self.assertRaises(FileExistsError):
                write_json(good,{'value':2})
            with self.assertRaises(ValueError):
                read_json(good,1)
            link = root / 'link.json'
            link.symlink_to(good)
            with self.assertRaises(ValueError):
                read_json(link)
            with self.assertRaises(ValueError):
                read_json(root / '..' / root.name / 'input.json')
            other = root / 'input.img'
            other.write_text('{}')
            with self.assertRaises(ValueError):
                read_json(other)


class CompactorTests(unittest.TestCase):
    RAW = r''' {
 "traceEvents" : [ { "name" : " a \\ \/ \" \u263A \t 雪 " } ]
} '''.encode()
    EXPECTED = r'''{"traceEvents":[{"name":" a \\ \/ \" \u263A \t 雪 "}]}'''.encode()

    def test_string_bytes_and_chunk_boundaries(self):
        lexical = compactor.JsonWhitespace()
        result = b''.join(lexical.feed(bytes([byte])) for byte in self.RAW)
        lexical.finish()
        self.assertEqual(result,self.EXPECTED)
        self.assertEqual(json.loads(result),json.loads(self.RAW))

    def test_archive_identity_exclusive_output_and_preservation(self):
        with tempfile.TemporaryDirectory(prefix='.chromium-compact-test-',dir=REPO) as temp:
            root = pathlib.Path(temp)
            source,output,manifest = root/'input.json.gz',root/'output.json',root/'manifest.json'
            archive = gzip.compress(self.RAW)
            source.write_bytes(archive)
            digest = hashlib.sha256(self.RAW).hexdigest()
            report = compactor.compact(source,output,manifest,digest)
            self.assertEqual(report['status'],'ACCEPTED_DERIVED_JSON')
            self.assertEqual(report['raw_input']['sha256_observed'],digest)
            self.assertEqual(output.read_bytes(),self.EXPECTED)
            self.assertEqual(source.read_bytes(),archive)
            again = compactor.compact(source,output,root/'second-manifest.json',digest)
            self.assertEqual(again['status'],'REJECTED')
            self.assertEqual(output.read_bytes(),self.EXPECTED)

    def test_budgets_truncation_and_wrong_hash(self):
        cases = [({'max_archive':1},None,None),({'max_decoded':len(self.RAW)-1},None,None),
                 ({'max_output':len(self.EXPECTED)-1},None,None),
                 ({},gzip.compress(self.RAW)[:-4],None),
                 ({},gzip.compress(b'{"traceEvents":['),b'{"traceEvents":['),
                 ({},None,'wrong-hash')]
        with tempfile.TemporaryDirectory(prefix='.chromium-compact-test-',dir=REPO) as temp:
            root = pathlib.Path(temp)
            for number,(options,archive,raw) in enumerate(cases):
                source,output,manifest = (root/f'{number}.json.gz',root/f'{number}.json',root/f'{number}-manifest.json')
                source.write_bytes(archive if archive is not None else gzip.compress(self.RAW))
                digest = '0'*64 if raw == 'wrong-hash' else hashlib.sha256(raw or self.RAW).hexdigest()
                result = compactor.compact(source,output,manifest,digest,**options)
                self.assertEqual(result['status'],'REJECTED')
                self.assertFalse(output.exists())
                self.assertTrue(source.exists())
            self.assertFalse(list(root.glob('.*.part-*')))


class ShardTests(unittest.TestCase):
    def make_shards(self,root):
        value={'traceEvents':[{'name':'one','ts':1,'args':{'unicode':'雪','value':1.25}},
                              {'name':'two','ts':9007199254740993,'args':{'zero':-0.0}},
                              {'name':'three','ts':3}], 'metadata':{'clock-domain':'test'}}
        raw=json.dumps(value,ensure_ascii=False).encode()
        archive=root/'source.json.gz';archive.write_bytes(gzip.compress(raw))
        out=root/'shards'
        manifest=sharder.shard_trace(archive,out,hashlib.sha256(raw).hexdigest(),shard_cap=110)
        return value,manifest,out

    def test_all_events_metadata_numbers_and_order_retained(self):
        with tempfile.TemporaryDirectory(prefix='.chromium-shard-test-',dir=REPO) as temp:
            value,manifest,out=self.make_shards(pathlib.Path(temp))
            result,identity=load_trace(out/'manifest.json',manifest=True)
            self.assertEqual(value,result)
            self.assertEqual(manifest['structural_sha256'],manifest['reconstructed_structural_sha256'])
            self.assertEqual(identity['verification']['trace_event_count'],3)
            with self.assertRaises(ValueError):
                load_trace(out/'manifest.json')

    def test_missing_reordered_tampered_traversal_and_aggregate(self):
        with tempfile.TemporaryDirectory(prefix='.chromium-shard-test-',dir=REPO) as temp:
            _,manifest,out=self.make_shards(pathlib.Path(temp))
            original=json.dumps(manifest)
            cases=[]
            changed=json.loads(original);changed['shards'][0]['file']='missing.json';cases.append(changed)
            changed=json.loads(original);changed['shards'].reverse();cases.append(changed)
            changed=json.loads(original);changed['shards'][0]['sha256']='0'*64;cases.append(changed)
            changed=json.loads(original);changed['shards'][0]['file']='../escape.json';cases.append(changed)
            changed=json.loads(original);changed['trace_event_count']=2;cases.append(changed)
            for index,changed in enumerate(cases):
                path=out/f'bad-{index}.json';path.write_text(json.dumps(changed))
                with self.assertRaises((ValueError,FileNotFoundError)):
                    load_trace(path,manifest=True)
            with mock.patch.object(common,'MAX_AGGREGATE',1):
                with self.assertRaisesRegex(ValueError,'aggregate'):
                    load_trace(out/'manifest.json',manifest=True)


if __name__ == '__main__':
    resource_limits()
    unittest.main()
