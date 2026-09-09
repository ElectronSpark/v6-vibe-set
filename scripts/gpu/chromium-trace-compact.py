#!/usr/bin/env python3
"""Losslessly remove JSON whitespace from an explicitly named gzip receipt.

The original archive remains untouched. A derived JSON is admitted only after
gzip/JSON validation, raw SHA256 verification, and the unchanged 64 MiB output
cap. The manifest retains original rejection, archive/raw/derived identities,
and any transformation failure. No events, numbers or string bytes are changed.
"""
import argparse
import gzip
import hashlib
import json
import os
import pathlib
import re
import stat
import tempfile

from chromium_trace_common import (checked_path, parser_identity, resource_limits,
                                    trace_events)

MAX_ARCHIVE = 64 * 1024 * 1024
MAX_DECODED = 128 * 1024 * 1024
MAX_OUTPUT = 64 * 1024 * 1024
CHUNK = 64 * 1024


class JsonWhitespace:
    def __init__(self):
        self.in_string = False
        self.escaped = False

    def feed(self, data):
        result = bytearray()
        for char in data:
            if self.in_string:
                result.append(char)
                if self.escaped:
                    self.escaped = False
                elif char == 92:
                    self.escaped = True
                elif char == 34:
                    self.in_string = False
            elif char == 34:
                self.in_string = True
                result.append(char)
            elif char not in (9, 10, 13, 32):
                result.append(char)
        return bytes(result)

    def finish(self):
        if self.in_string:
            raise ValueError('unterminated JSON string')


def compact(source, output, manifest, expected_raw_sha256, *,
            max_archive=MAX_ARCHIVE, max_decoded=MAX_DECODED, max_output=MAX_OUTPUT):
    if not re.fullmatch('[0-9a-f]{64}', expected_raw_sha256):
        raise ValueError('expected raw SHA256 must be 64 lowercase hexadecimal characters')
    if not (0 < max_archive <= MAX_ARCHIVE and 0 < max_decoded <= MAX_DECODED
            and 0 < max_output <= MAX_OUTPUT):
        raise ValueError('budgets must be positive and cannot exceed fixed safety caps')
    source, output, manifest = map(checked_path, (source, output, manifest))
    if not source.name.lower().endswith('.json.gz') or output.suffix.lower() != '.json' or manifest.suffix.lower() != '.json':
        raise ValueError('requires an explicitly named .json.gz input and .json outputs')
    if output == manifest:
        raise ValueError('derived output and manifest must differ')
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest.parent.mkdir(parents=True, exist_ok=True)
    report = {'schema_version':1, 'status':'REJECTED', 'source_archive':{'path':str(source)},
              'raw_input':{'expected_sha256':expected_raw_sha256}, 'derived_output':None,
              'original_admission':'Rejected raw JSON export; deriving a compact receipt does not retroactively accept the original export.',
              'transformation':'Remove only ASCII space, tab, CR and LF outside JSON strings; preserve every other byte and its order.',
              'limits_bytes':{'archive':max_archive,'decompressed':max_decoded,'derived':max_output},
              'parser_inputs':parser_identity(__file__)}
    temporary = None
    raw_digest, output_digest = hashlib.sha256(), hashlib.sha256()
    raw_count = output_count = 0
    complete = False
    output_overflow = False
    # Reserve the manifest first: neither receipts nor derived outputs may be overwritten.
    with manifest.open('x') as receipt:
        try:
            if output.exists():
                raise FileExistsError('derived output already exists: ' + str(output))
            fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            with os.fdopen(fd, 'rb') as archive:
                before = os.fstat(archive.fileno())
                if not stat.S_ISREG(before.st_mode) or before.st_size > max_archive:
                    raise ValueError('archive must be a bounded regular file')
                archive_digest = hashlib.sha256()
                archive_count = 0
                while True:
                    part = archive.read(min(CHUNK, max_archive + 1 - archive_count))
                    if not part:
                        break
                    archive_digest.update(part)
                    archive_count += len(part)
                    if archive_count > max_archive:
                        raise ValueError('archive exceeds size budget')
                report['source_archive'].update(bytes=archive_count,sha256=archive_digest.hexdigest())
                archive.seek(0)
                lexical = JsonWhitespace()
                with tempfile.NamedTemporaryFile(mode='w+b', prefix='.'+output.name+'.part-',
                                                 dir=output.parent, delete=False) as derived:
                    temporary = pathlib.Path(derived.name)
                    with gzip.GzipFile(fileobj=archive, mode='rb') as compressed:
                        while True:
                            part = compressed.read(min(CHUNK, max_decoded + 1 - raw_count))
                            if not part:
                                break
                            raw_digest.update(part)
                            raw_count += len(part)
                            if raw_count > max_decoded:
                                raise ValueError('decompressed JSON exceeds size budget')
                            packed = lexical.feed(part)
                            output_count += len(packed)
                            if output_count > max_output:
                                output_overflow = True
                            output_digest.update(packed)
                            if not output_overflow:
                                derived.write(packed)
                    complete = True
                    lexical.finish()
                    if raw_digest.hexdigest() != expected_raw_sha256:
                        raise ValueError('decompressed raw SHA256 does not match retained receipt')
                    after = os.fstat(archive.fileno())
                    if (before.st_size,before.st_mtime_ns) != (after.st_size,after.st_mtime_ns):
                        raise ValueError('archive changed while being read')
                    if output_overflow:
                        raise ValueError('compact JSON still exceeds unchanged output cap; completed bounded identity/count pass without publication')
                    derived.flush()
                    derived.seek(0)
                    def invalid_constant(value):
                        raise ValueError('invalid JSON constant: ' + value)
                    value = json.load(derived, parse_constant=invalid_constant)
                    events = trace_events(value)
                    event_count = len(events)
                    del events, value
                # Atomic exclusive publication: source archive and prior receipts stay intact.
                os.link(temporary, output)
                report.update(status='ACCEPTED_DERIVED_JSON',
                              trace_event_count=event_count,
                              removed_whitespace_bytes=raw_count-output_count,
                              derived_output={'path':str(output),'bytes':output_count,
                                              'sha256':output_digest.hexdigest()},
                              validation='gzip trailer, UTF-8/JSON syntax, expected raw SHA256, event-list bound, and output byte cap passed')
        except Exception as error:
            report['error'] = type(error).__name__ + ': ' + str(error)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
            report['raw_input'].update(bytes_observed=raw_count,sha256_observed=raw_digest.hexdigest(),
                                       complete_decompression=complete)
            report['compact_bytes_observed'] = output_count
            report['compact_sha256_observed'] = output_digest.hexdigest()
            receipt.write(json.dumps(report,indent=2,allow_nan=False)+'\n')
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archive')
    ap.add_argument('--output',required=True)
    ap.add_argument('--manifest',required=True)
    ap.add_argument('--expected-raw-sha256',required=True)
    args = ap.parse_args()
    resource_limits()
    report = compact(args.archive,args.output,args.manifest,args.expected_raw_sha256)
    print(json.dumps(report))
    raise SystemExit(0 if report['status']=='ACCEPTED_DERIVED_JSON' else 1)


if __name__ == '__main__':
    main()
