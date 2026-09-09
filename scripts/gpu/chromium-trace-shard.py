#!/usr/bin/env python3
"""Retain every event from a bounded, hash-matched gzip trace in ordered shards.

Each shard is at most 32 MiB. The explicit manifest includes all trace metadata,
archive/raw identities and structural hashes before and after reconstruction.
Original oversized exports remain rejected. JSON numeric semantics are the same
as the existing Python analyzers; no events or fields are removed or reordered.
"""
import argparse
import hashlib
import json
import pathlib

from chromium_trace_common import (MAX_AGGREGATE,MAX_INPUT,MAX_SHARD,SHARD_SCHEMA,
    checked_path,canonical_json,parser_identity,read_gzip_trace,resource_limits,
    structure_hasher,trace_events,verify_trace_manifest,write_json)


def shard_trace(archive, output_dir, expected_raw_sha256, shard_cap=MAX_SHARD):
    if not 32 <= shard_cap <= MAX_SHARD:
        raise ValueError('invalid shard cap')
    output_dir = checked_path(output_dir)
    output_dir.mkdir(parents=True,exist_ok=False)
    try:
        trace, raw, source = read_gzip_trace(archive,expected_raw_sha256)
        events = trace_events(trace)
        shape = 'object' if isinstance(trace,dict) else 'array'
        metadata = {k:v for k,v in trace.items() if k!='traceEvents'} if shape=='object' else {}
        encoded = json.dumps(metadata,separators=(',',':'),ensure_ascii=False,allow_nan=False).encode()
        if len(encoded) > MAX_INPUT:
            raise ValueError('metadata exceeds per-file 64 MiB cap')
        def write_member(name, data):
            with (output_dir/name).open('xb') as stream:
                stream.write(data)
            return {'file':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
        metadata_descriptor = write_member('trace-metadata.json',encoded)
        original_structure = structure_hasher(shape,metadata)
        descriptors = []
        parts, part_bytes, event_start = [], 0, 0
        prefix, suffix = b'{"traceEvents":[', b']}'
        member_bytes = len(encoded)
        def flush():
            nonlocal parts,part_bytes,event_start,member_bytes
            data = prefix+b','.join(parts)+suffix
            if len(data)>shard_cap:
                raise ValueError('single trace event exceeds shard cap')
            descriptor = write_member(f'trace-events-{len(descriptors):05d}.json',data)
            descriptor.update(index=len(descriptors),event_start=event_start,event_count=len(parts))
            descriptors.append(descriptor)
            member_bytes += len(data)
            if member_bytes > MAX_AGGREGATE or len(descriptors)>128:
                raise ValueError('aggregate shard budget exceeded')
            event_start += len(parts)
            parts,part_bytes = [],0
        for event in events:
            original_structure.update(canonical_json(event)+b'\n')
            data = json.dumps(event,separators=(',',':'),ensure_ascii=False,allow_nan=False).encode()
            if parts and len(prefix)+part_bytes+len(parts)+len(data)+len(suffix)>shard_cap:
                flush()
            if len(prefix)+len(data)+len(suffix)>shard_cap:
                raise ValueError('single trace event exceeds shard cap')
            parts.append(data)
            part_bytes += len(data)
        if parts or not descriptors:
            flush()
        manifest = {'schema':SHARD_SCHEMA,'status':'ACCEPTED_COMPLETE_SHARDS',
            'trace_json_shape':shape,'source_archive':source,'raw_input':raw,
            'metadata':metadata_descriptor,'shards':descriptors,'member_bytes':member_bytes,
            'trace_event_count':len(events),'structural_sha256':original_structure.hexdigest(),
            'structural_hash_method':'SHA256 of chromium-trace-structure-v1\\n, shape\\n, canonical metadata\\n, then each canonical event\\n in original order. Canonical JSON sorts object keys only, uses UTF-8 and Python JSON numeric semantics.',
            'original_admission':'Original raw export remains rejected for exceeding 64 MiB; explicit complete-shard manifest is a separate derived input.',
            'limits':{'per_source_file_bytes':MAX_INPUT,'shard_bytes':MAX_SHARD,
                      'aggregate_bytes':MAX_AGGREGATE,'max_events':1500000},
            'parser_inputs':parser_identity(__file__)}
        _, verification = verify_trace_manifest(manifest,output_dir,collect=False)
        manifest['reconstructed_structural_sha256'] = verification['structural_sha256']
        encoded_manifest = (json.dumps(manifest,indent=2,allow_nan=False)+'\n').encode()
        if len(encoded_manifest)>1024*1024 or member_bytes+len(encoded_manifest)>MAX_AGGREGATE:
            raise ValueError('manifest plus member aggregate exceeds budget')
        write_member('manifest.json',encoded_manifest)
        return manifest
    except Exception as error:
        write_json(output_dir/'rejected-transformation.json',{
            'status':'REJECTED','archive':str(archive),'expected_raw_sha256':expected_raw_sha256,
            'error':type(error).__name__+': '+str(error),'parser_inputs':parser_identity(__file__)})
        raise


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archive')
    ap.add_argument('--output-dir',required=True)
    ap.add_argument('--expected-raw-sha256',required=True)
    args=ap.parse_args()
    resource_limits()
    manifest=shard_trace(args.archive,args.output_dir,args.expected_raw_sha256)
    print(json.dumps({'manifest':str(pathlib.Path(args.output_dir)/'manifest.json'),
                      **{k:manifest[k] for k in ('raw_input','member_bytes','trace_event_count',
                        'structural_sha256','reconstructed_structural_sha256')},
                      'shards':[{'bytes':s['bytes'],'events':s['event_count']} for s in manifest['shards']]}))


if __name__=='__main__':
    main()
