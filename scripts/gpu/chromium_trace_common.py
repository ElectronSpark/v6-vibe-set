"""Bounded local JSON evidence I/O shared by offline Chromium analyzers."""
import hashlib
import gzip
import json
import math
import os
import pathlib
import resource
import re
import signal
import stat

REPO = pathlib.Path(__file__).resolve().parents[2]
MAX_INPUT = 64 * 1024 * 1024
MAX_EVENTS = 1_500_000
MAX_AGGREGATE = 128 * 1024 * 1024
MAX_SHARD = 32 * 1024 * 1024
SHARD_SCHEMA = 'chromium-trace-shards-v1'


def resource_limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
    signal.alarm(90)


def checked_path(path):
    p = pathlib.Path(path)
    if '..' in p.parts:
        raise ValueError('parent traversal is forbidden: ' + str(p))
    p = p.absolute()
    if any(part.is_symlink() for part in (p, *p.parents)):
        raise ValueError('symlink path is forbidden: ' + str(p))
    if not p.resolve().is_relative_to(REPO):
        raise ValueError('evidence must be inside repository: ' + str(p))
    return p


def read_json(path, limit=MAX_INPUT):
    p = checked_path(path)
    if p.suffix.lower() != '.json':
        raise ValueError('requires an explicitly named JSON file: ' + str(p))
    with os.fdopen(os.open(p, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK), 'rb') as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
            raise ValueError('requires a bounded regular file: ' + str(p))
        raw = source.read(limit + 1)
        after = os.fstat(source.fileno())
    if len(raw) > limit or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError('oversized or changed input: ' + str(p))
    return json.loads(raw), {'path': str(p), 'bytes': len(raw),
                             'sha256': hashlib.sha256(raw).hexdigest()}


def write_json(path, value, limit=MAX_INPUT):
    p = checked_path(path)
    raw = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode()
    if len(raw) > limit:
        raise ValueError('output exceeds size budget: ' + str(p))
    # Refuse to overwrite historical receipts, including on an accidental rerun.
    with p.open('xb') as output:
        output.write(raw)
    return {'path': str(p), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def trace_events(trace):
    events = trace.get('traceEvents') if isinstance(trace, dict) else trace
    if not isinstance(events, list) or len(events) > MAX_EVENTS:
        raise ValueError('traceEvents must contain at most 1.5 million events')
    return events


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def parser_identity(path):
    return {str(pathlib.Path(p).name): hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
            for p in (path, __file__)}


def canonical_json(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False,
                      allow_nan=False).encode('utf-8')


def structure_hasher(shape, metadata):
    digest = hashlib.sha256(b'chromium-trace-structure-v1\n')
    digest.update(shape.encode('ascii') + b'\n')
    digest.update(canonical_json(metadata) + b'\n')
    return digest


def read_gzip_trace(path, expected_raw_sha256, max_decoded=MAX_AGGREGATE):
    """Explicit bounded transformation input; never a recursive archive search."""
    p = checked_path(path)
    if not p.name.lower().endswith('.json.gz'):
        raise ValueError('requires explicitly named .json.gz receipt')
    if not re.fullmatch('[0-9a-f]{64}',expected_raw_sha256):
        raise ValueError('invalid expected raw SHA256')
    if not 0 < max_decoded <= MAX_AGGREGATE:
        raise ValueError('invalid decompression budget')
    with os.fdopen(os.open(p,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK),'rb') as archive:
        before = os.fstat(archive.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > MAX_INPUT:
            raise ValueError('archive must be regular and at most 64 MiB')
        archive_hash = hashlib.sha256()
        archive_count = 0
        while True:
            part = archive.read(min(65536,MAX_INPUT+1-archive_count))
            if not part:
                break
            archive_hash.update(part)
            archive_count += len(part)
            if archive_count > MAX_INPUT:
                raise ValueError('archive exceeds 64 MiB')
        archive.seek(0)
        raw = bytearray()
        raw_hash = hashlib.sha256()
        with gzip.GzipFile(fileobj=archive,mode='rb') as source:
            while True:
                part = source.read(min(65536,max_decoded+1-len(raw)))
                if not part:
                    break
                raw.extend(part)
                raw_hash.update(part)
                if len(raw) > max_decoded:
                    raise ValueError('decompressed input exceeds 128 MiB budget')
        after = os.fstat(archive.fileno())
    if (before.st_size,before.st_mtime_ns) != (after.st_size,after.st_mtime_ns):
        raise ValueError('archive changed during read')
    if raw_hash.hexdigest() != expected_raw_sha256:
        raise ValueError('raw SHA256 does not match retained export receipt')
    def invalid_constant(value):
        raise ValueError('invalid JSON constant: ' + value)
    trace = json.loads(raw,parse_constant=invalid_constant)
    trace_events(trace)
    return trace, {'bytes':len(raw),'sha256':raw_hash.hexdigest()}, {
        'path':str(p),'bytes':archive_count,'sha256':archive_hash.hexdigest()}


def verify_trace_manifest(manifest, directory, manifest_bytes=0, collect=True):
    """Validate ordered shards, then optionally reconstruct the ordinary trace."""
    if (not isinstance(manifest,dict) or manifest.get('schema') != SHARD_SCHEMA
            or manifest.get('status') != 'ACCEPTED_COMPLETE_SHARDS'):
        raise ValueError('requires accepted explicit Chromium event-shard manifest')
    directory = checked_path(directory)
    shape = manifest.get('trace_json_shape')
    if shape not in ('object','array'):
        raise ValueError('invalid original JSON shape')
    count = manifest.get('trace_event_count')
    if type(count) is not int or not 0 <= count <= MAX_EVENTS:
        raise ValueError('invalid aggregate event count')
    shards = manifest.get('shards')
    if not isinstance(shards,list) or not 1 <= len(shards) <= 128:
        raise ValueError('requires between one and 128 explicit shards')
    aggregate = manifest_bytes
    identities = []
    seen = set()
    def read_member(descriptor, cap):
        nonlocal aggregate
        if not isinstance(descriptor,dict):
            raise ValueError('invalid member descriptor')
        name, size, digest = (descriptor.get(k) for k in ('file','bytes','sha256'))
        if (not isinstance(name,str) or pathlib.Path(name).name != name
                or name in ('.','..') or name in seen):
            raise ValueError('member must have a unique basename without traversal')
        if type(size) is not int or not 0 <= size <= cap:
            raise ValueError('invalid member byte count')
        if not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest):
            raise ValueError('invalid member SHA256')
        aggregate += size
        if aggregate > MAX_AGGREGATE:
            raise ValueError('manifest aggregate exceeds 128 MiB')
        seen.add(name)
        value, identity = read_json(directory/name,cap)
        if identity['bytes'] != size or identity['sha256'] != digest:
            raise ValueError('member identity mismatch: ' + name)
        identities.append(identity)
        return value
    metadata = read_member(manifest.get('metadata'),MAX_INPUT)
    if not isinstance(metadata,dict) or 'traceEvents' in metadata or (shape == 'array' and metadata):
        raise ValueError('invalid trace metadata')
    digest = structure_hasher(shape,metadata)
    all_events = [] if collect else None
    position = 0
    for index, descriptor in enumerate(shards):
        if descriptor.get('index') != index or descriptor.get('event_start') != position:
            raise ValueError('shard ordering/start index mismatch')
        value = read_member(descriptor,MAX_SHARD)
        if not isinstance(value,dict) or set(value) != {'traceEvents'}:
            raise ValueError('event shard must contain only traceEvents')
        events = trace_events(value)
        if type(descriptor.get('event_count')) is not int or len(events) != descriptor['event_count']:
            raise ValueError('shard event count mismatch')
        position += len(events)
        if position > MAX_EVENTS or position > count:
            raise ValueError('aggregate event count exceeds declared limit')
        for event in events:
            digest.update(canonical_json(event)+b'\n')
        if collect:
            all_events.extend(events)
    if position != count or digest.hexdigest() != manifest.get('structural_sha256'):
        raise ValueError('reconstructed count/structural identity mismatch')
    if aggregate-manifest_bytes != manifest.get('member_bytes'):
        raise ValueError('aggregate byte count mismatch')
    trace = (dict(metadata,traceEvents=all_events) if shape == 'object' else all_events) if collect else None
    return trace, {'members':identities,'aggregate_bytes':aggregate,
                   'trace_event_count':position,'structural_sha256':digest.hexdigest()}


def load_trace(path, manifest=False):
    """Return (ordinary trace JSON, identity); manifest admission is explicit."""
    if not manifest:
        trace, identity = read_json(path)
        trace_events(trace)
        return trace, identity
    descriptor, identity = read_json(path,1024*1024)
    trace, verification = verify_trace_manifest(descriptor,pathlib.Path(identity['path']).parent,
                                                identity['bytes'])
    identity.update(format=SHARD_SCHEMA,verification=verification,
                    original_raw_input=descriptor.get('raw_input'),
                    original_archive_input=descriptor.get('source_archive'))
    return trace, identity
