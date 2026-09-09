#!/usr/bin/env python3
"""Bounded Chromium 151 JSON trace receive/attempt correlation; no ACK inference.

Usage: analyze-mojo-video-boundary.py TRACE.json OUTPUT.json
       analyze-mojo-video-boundary.py --self-test
Only completed X scopes count. Unrepresented phases are reported explicitly.
"""
import argparse
import bisect
import collections
import hashlib
import json
import pathlib
import re
import resource
import signal
import sys

from chromium_trace_common import (read_json, write_json, resource_limits, number,
                                    parser_identity, trace_events, checked_path, load_trace)

MAX_OUTPUT = 32 * 1024 * 1024
SUBMIT = 'VideoFrameSubmitter::SubmitFrame'
BEGIN = 'VideoFrameSubmitter::OnBeginFrame'
APPEND = 'VideoFrameResourceProvider::AppendQuads'
METHODS = {
    'CompositorFrameSinkClient': ['DidReceiveCompositorFrameAck', 'OnBeginFrame',
                                'ReclaimResources', 'OnBeginFramePausedChanged'],
    'FrameSinkBundleClient': ['FlushNotifications', 'OnBeginFramePausedChanged'],
    'CompositorFrameSink': ['SubmitCompositorFrame', 'DidNotProduceFrame'],
    'FrameSinkBundle': ['Submit', 'SetNeedsBeginFrame'],
}


def method_table():
    rows = []
    for iface, methods in METHODS.items():
        for method in methods:
            name = 'viz::mojom::' + iface + '::' + method
            rows.append({'interface_tag': 'viz.mojom.' + iface, 'method': method,
                         'ipc_hash': int(hashlib.sha256(name.encode()).hexdigest()[:8], 16),
                         'name': name})
    assert len({(r['interface_tag'], r['ipc_hash']) for r in rows}) == len(rows)
    return rows


TABLE = method_table()
BY_HASH = {(r['interface_tag'], r['ipc_hash']): r for r in TABLE}


def identify_receive(e):
    """Require both interface tag and hash; ignore similarly named cc ACKs."""
    if not {'mojom', 'toplevel'}.intersection(e.get('cat', '').split(',')):
        return None
    info = e.get('args', {}).get('chrome_mojo_event_info', {})
    if not isinstance(info, dict):
        return None
    raw_hash = info.get('ipc_hash')
    try:
        value = int(raw_hash, 0) if isinstance(raw_hash, str) else int(raw_hash)
    except (TypeError, ValueError, OverflowError):
        return None
    method = BY_HASH.get((info.get('mojo_interface_tag'), value))
    if method is None:
        return None
    # The pinned generated bindings distinguish responses by event name.
    # chrome_mojo_event_info does not carry a response boolean; payload size
    # and data_num_bytes do not expose ACK-array membership.
    valid_names = {'Receive mojo message', 'Receive ' + method['name'],
                   'Receive ' + method['name'].replace('viz::mojom::', 'viz::mojom::blink::')}
    return method if e.get('name') in valid_names else None


def end(e):
    return e['ts'] + e['dur']


def coord(e):
    return e['pid'], e['tid']


def short(e):
    return {'event_index': e['_index'], 'ts_us': e['ts'], 'end_us': end(e),
            'duration_us': e['dur']}


def contains(outer, inner):
    if coord(outer) != coord(inner) or outer['dur'] <= 0:
        return False
    # Equal-bound scopes and zero-duration boundary siblings are ambiguous.
    if inner['dur'] == 0:
        return outer['ts'] < inner['ts'] < end(outer)
    return (outer['ts'] <= inner['ts'] and end(inner) <= end(outer)
            and (outer['ts'] < inner['ts'] or end(inner) < end(outer)))


def ambiguous_parent(outer, inner):
    return (coord(outer) == coord(inner) and outer['ts'] <= inner['ts']
            and end(inner) <= end(outer) and not contains(outer, inner))


def pts(e):
    frame = e.get('args', {}).get('frame', '')
    m = re.search(r'\btimestamp:(-?\d+)', frame)
    return int(m.group(1)) if m else None


def analyze(events, alignment=None):
    trace_events(events)
    if alignment is not None:
        if (alignment.get('validated') is not True or not alignment.get('method')
                or not alignment.get('evidence')):
            raise ValueError('alignment requires validated=true, method and evidence')
        window_start, window_end, uncertainty = (alignment.get(k) for k in ('trace_start_us', 'trace_end_us', 'uncertainty_us'))
        if (not all(number(v) for v in (window_start,window_end,uncertainty))
                or window_end <= window_start or uncertainty < 0):
            raise ValueError('invalid alignment window/uncertainty')
    def inside(e):
        return alignment is None or window_start <= e['ts'] <= window_end
    valid_events, malformed, excluded_replies = [], [], 0
    for index, original in enumerate(events):
        if not isinstance(original, dict):
            malformed.append({'index':index, 'reason':'non-object event'})
            continue
        e = dict(original, _index=index)
        if (not isinstance(e.get('args', {}), dict) or not isinstance(e.get('cat', ''), str)
                or not isinstance(e.get('name', ''), str)):
            malformed.append({'index':index, 'reason':'invalid arguments or category'})
            continue
        if e.get('ph') == 'X' and (not all(number(e.get(k)) for k in ('ts','dur'))
                                   or e['dur'] < 0 or 'pid' not in e or 'tid' not in e):
            malformed.append({'index':index, 'reason':'invalid complete scope'})
            continue
        if e.get('name', '').startswith('Receive mojo reply') or e.get('name', '').startswith('Receive reply '):
            excluded_replies += 1
        valid_events.append(e)
    events = valid_events
    receives = []
    rejected_phases = collections.Counter()
    for index, e in enumerate(events):
        method = identify_receive(e)
        if method:
            if e.get('ph') == 'X':
                receives.append((e, method))
            else:
                rejected_phases['receive:' + e.get('ph', '?')] += 1
        elif e.get('name') in (SUBMIT, BEGIN, APPEND) and e.get('ph') != 'X':
            rejected_phases[e['name'] + ':' + e.get('ph', '?')] += 1
    video_coords = sorted({coord(e) for e in events if e.get('name') == SUBMIT and e.get('ph') == 'X'})
    result = {'schema_version': 2, 'method_hashes': TABLE,
              'alignment': alignment, 'malformed_event_count':len(malformed),
              'malformed_event_examples':malformed[:100],
              'excluded_reply_scopes':excluded_replies,
              'unrepresented_target_phases': dict(rejected_phases),
              'identified_receive_scopes_all_threads': len(receives),
              'video_threads': [], 'pending_ack_count': None,
              'limits': [
                  'ACK counters and rejection reasons are not instrumented; pending_ack_count remains unknown.',
                  'A FlushNotifications receipt does not establish a nonempty ACK array.',
                  'Direct ACK receipts are assigned only to a process/thread, not a sink instance.',
                  'One fixture video does not by itself prove one internal submitter instance or absence of resets.',
                  'Only completed X scopes count; unmatched/non-X target phases are retained as a coverage warning.',
                  'Full-trace rows remain available; playback fields use only the independently validated alignment when supplied.',
                  'Equal-bound scopes and zero-duration boundary siblings cannot prove nesting; ambiguous construction remains unknown.',
                  'Nested/reentrant receives use separately sorted start/end indexes for nearest endpoint queries.',
                  'Scope durations are elapsed dispatch work, not thread CPU or physical display latency.',
              ]}
    names = {(e['pid'], e['tid']): e.get('args', {}).get('name') for e in events
             if e.get('ph') == 'M' and e.get('name') == 'thread_name'
             and 'pid' in e and 'tid' in e}
    for key in video_coords:
        ev = sorted((e for e in events if e.get('ph') == 'X' and coord(e) == key), key=lambda e: (e['ts'], -e['dur']))
        attempts = [e for e in ev if e.get('name') == SUBMIT]
        begins = [e for e in ev if e.get('name') == BEGIN]
        appends = [e for e in ev if e.get('name') == APPEND]
        recv = sorted(((e, m) for e, m in receives if coord(e) == key), key=lambda pair: pair[0]['ts'])
        acks = [e for e, m in recv if m['method'] == 'DidReceiveCompositorFrameAck']
        bundles = [e for e, m in recv if m['method'] == 'FlushNotifications']
        direct_begins = [e for e, m in recv if m['method'] == 'OnBeginFrame']
        acks_by_end = sorted(acks, key=lambda e: (end(e), e['ts'], e['_index']))
        bundles_by_end = sorted(bundles, key=lambda e: (end(e), e['ts'], e['_index']))
        ack_ends = [end(e) for e in acks_by_end]
        ack_starts = [e['ts'] for e in acks]
        bundle_ends = [end(e) for e in bundles_by_end]
        append_owners = {}
        ambiguous = []
        possible_owners = collections.defaultdict(set)
        for app in appends:
            owners = [attempt for attempt in attempts if contains(attempt, app)]
            weak = [attempt for attempt in attempts if attempt['ts'] <= app['ts'] and end(app) <= end(attempt)]
            if len(owners) == 1 and len(weak) == 1:
                append_owners[app['_index']] = owners[0]['_index']
            elif weak:
                ambiguous.append({'append_event_index':app['_index'],
                                  'possible_submit_event_indices':[o['_index'] for o in weak]})
                for attempt in weak:
                    possible_owners[attempt['_index']].add(app['_index'])
        admitted_indices = set(append_owners.values())
        attempt_rows, previous_admission = [], None
        for attempt in attempts:
            nested_append = (None if attempt['_index'] in possible_owners
                             else attempt['_index'] in admitted_indices)
            prior = bisect.bisect_right(ack_ends, attempt['ts']) - 1
            following = bisect.bisect_left(ack_starts, end(attempt))
            prior_bundle = bisect.bisect_right(bundle_ends, attempt['ts']) - 1
            parent_begins = [b for b in begins if contains(b, attempt)]
            parent_receives = [(r, m) for r, m in recv if contains(r, attempt)]
            row = {**short(attempt), 'pts_us': pts(attempt), 'frame_constructed': nested_append,
                   'starts_in_playback_window':inside(attempt) if alignment else None,
                   'natural_size': None, 'submitter_begin_frame_indices': [b['_index'] for b in parent_begins],
                   'enclosing_receives': [{'event_index': r['_index'], 'method': m['method']} for r, m in parent_receives],
                   'ambiguous_parent_scope_indices': [e['_index'] for e in begins + [r for r,m in recv]
                                                      if ambiguous_parent(e, attempt)],
                   'previous_direct_ack': short(acks_by_end[prior]) if prior >= 0 else None,
                   'next_direct_ack': short(acks[following]) if following < len(acks) else None,
                   'previous_bundle_receipt': short(bundles_by_end[prior_bundle]) if prior_bundle >= 0 else None,
                   'previous_construction': previous_admission,
                   'direct_acks_since_previous_construction': None}
            m = re.search(r'\bnatural_size:(\d+)x(\d+)', attempt.get('args', {}).get('frame', ''))
            if m:
                row['natural_size'] = [int(x) for x in m.groups()]
            if previous_admission:
                row['direct_acks_since_previous_construction'] = sum(
                    previous_admission['end_us'] <= a['ts'] and end(a) <= attempt['ts'] for a in acks)
            attempt_rows.append(row)
            if nested_append:
                previous_admission = short(attempt)
        begin_rows = []
        for b in begins:
            parents = [(r, m) for r, m in recv if contains(r, b)]
            begin_rows.append({**short(b), 'enclosing_receives': [
                {**short(r), 'method': m['method'], 'interface_tag': m['interface_tag']} for r, m in parents],
                'ambiguous_parent_receive_indices':[r['_index'] for r,m in recv if ambiguous_parent(r,b)]})
        result['video_threads'].append({
            'pid': key[0], 'tid': key[1], 'thread_name': names.get(key),
            'submission_attempts': len(attempts), 'constructions': sum(r['frame_constructed'] is True for r in attempt_rows),
            'ambiguous_construction_attempts':sum(r['frame_constructed'] is None for r in attempt_rows),
            'ambiguous_append_containment':ambiguous,
            'playback_window': ({
                'submission_attempts':sum(inside(e) for e in attempts),
                'constructions':sum(r['frame_constructed'] is True and r['starts_in_playback_window'] for r in attempt_rows),
                'direct_ack_receives':sum(inside(e) for e in acks),
                'bundle_receives':sum(inside(e) for e in bundles),
                'direct_begin_frame_receives':sum(inside(e) for e in direct_begins),
            } if alignment else None),
            'direct_ack_receive_count': len(acks), 'bundle_receive_count': len(bundles),
            'direct_begin_frame_receive_count': len(direct_begins),
            'bundle_ack_contents_observed': False,
            'receive_scopes': [{**short(r), 'method': m['method'], 'interface_tag': m['interface_tag'],
                                'mojo_info': r.get('args', {}).get('chrome_mojo_event_info', {})} for r, m in recv],
            'attempts': attempt_rows, 'begin_frames': begin_rows,
        })
    return result


def self_test():
    def event(name, ts, dur, tid=2, args=None, cat='media'):
        return {'name': name, 'ts': ts, 'dur': dur, 'pid': 1, 'tid': tid, 'ph': 'X', 'cat': cat, 'args': args or {}}
    def recv(iface, method, ts, dur, tid=2):
        row = next(r for r in TABLE if r['interface_tag'].endswith('.' + iface) and r['method'] == method)
        return event('Receive mojo message', ts, dur, tid,
                     {'chrome_mojo_event_info': {'mojo_interface_tag': row['interface_tag'], 'ipc_hash': row['ipc_hash']}}, 'toplevel,mojom')
    events = [event(SUBMIT, 1, 2), event(APPEND, 1.5, .2),
              recv('CompositorFrameSinkClient', 'DidReceiveCompositorFrameAck', 4, 1, 99),
              recv('FrameSinkBundleClient', 'FlushNotifications', 10, 5),
              event(BEGIN, 11, 3), event(SUBMIT, 12, 1),
              recv('CompositorFrameSinkClient', 'DidReceiveCompositorFrameAck', 20, 1),
              event(SUBMIT, 22, 1)]
    r = analyze(events); v = r['video_threads'][0]
    assert v['direct_ack_receive_count'] == 1 and v['bundle_receive_count'] == 1
    assert v['attempts'][1]['direct_acks_since_previous_construction'] == 0
    assert v['attempts'][2]['direct_acks_since_previous_construction'] == 1
    assert v['attempts'][1]['enclosing_receives'][0]['method'] == 'FlushNotifications'
    assert v['attempts'][0]['frame_constructed'] and not v['attempts'][1]['frame_constructed']
    assert r['pending_ack_count'] is None and not v['bundle_ack_contents_observed']
    spoof = recv('CompositorFrameSinkClient', 'DidReceiveCompositorFrameAck', 1, 1)
    spoof['args']['chrome_mojo_event_info']['mojo_interface_tag'] = 'unrelated'
    assert identify_receive(spoof) is None
    return {'passed': True, 'checks': ['unrelated-thread ACK excluded', 'bundle not counted as ACK',
            'interface/hash pair required', 'construction nesting', 'unknown initial ACK count retained',
            'direct ACK/attempt interval correlation']}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('trace', nargs='?')
    ap.add_argument('output', nargs='?')
    ap.add_argument('--alignment')
    ap.add_argument('--trace-manifest', action='store_true',help='Positional TRACE is an explicit complete event-shard manifest')
    ap.add_argument('--run-dir')
    ap.add_argument('--trial')
    ap.add_argument('--output-dir')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    resource_limits()
    if args.self_test:
        print(json.dumps(self_test(), indent=2))
        return
    if args.run_dir:
        if not args.trial or not args.output_dir or args.trace or args.output:
            ap.error('--run-dir requires --trial and --output-dir, without positional files')
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}', args.trial):
            ap.error('invalid trial label')
        root, output_dir = checked_path(args.run_dir), checked_path(args.output_dir)
        if root == output_dir:
            ap.error('output directory must differ from source run directory')
        output_dir.mkdir(parents=True, exist_ok=True)
        args.trace = root / (args.trial + '-chromium-trace.json')
        args.output = output_dir / (args.trial + '-mojo-boundary.json')
    elif not args.trace or not args.output or args.trial or args.output_dir:
        ap.error('provide TRACE OUTPUT, or --run-dir/--trial/--output-dir')
    data, identity = load_trace(args.trace,manifest=args.trace_manifest)
    alignment, alignment_identity = read_json(args.alignment, 65536) if args.alignment else (None,None)
    report = analyze(trace_events(data), alignment)
    report['input'] = identity
    report['alignment_input'] = alignment_identity
    report['parser_inputs'] = parser_identity(__file__)
    output = write_json(args.output, report, MAX_OUTPUT)
    print(json.dumps({'output': output, 'threads': [
        {k:v[k] for k in ('pid','tid','submission_attempts','constructions',
                         'ambiguous_construction_attempts','direct_ack_receive_count',
                         'bundle_receive_count','playback_window')}
        for v in report['video_threads']]}))


if __name__ == '__main__':
    main()
