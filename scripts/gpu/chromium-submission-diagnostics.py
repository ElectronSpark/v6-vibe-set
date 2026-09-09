#!/usr/bin/env python3
"""Bounded offline analysis of the opt-in VideoBottleneck diagnostic schema.

Diagnostic IDs are local observation identities. PTS and generic Mojo bundles
never supply missing ACK/frame identity. Missing, duplicate, reset and censored
records remain explicit instead of being repaired by timing or subtraction.
"""
import argparse
import collections
import importlib.util
import json
import pathlib
import re

import chromium_mojo_flow

from chromium_trace_common import (load_trace, number, parser_identity, read_json,
                                    resource_limits, trace_events, write_json)

spec = importlib.util.spec_from_file_location(
    'submission_inventory', pathlib.Path(__file__).with_name('chromium-trace-inventory.py'))
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)

EVENT = 'VideoBottleneck'
CATEGORY = 'disabled-by-default-media.bottleneck'
OBJECTS = {'submitter', 'client_bundle', 'service_group', 'service_bundle', 'service_sink', 'surface', 'frame',
           'surface_ack', 'preparation', 'selection'}
REASONS = {'no_sink', 'not_visible', 'duplicate_frame', 'empty_output', 'pending_ack', 'accepted'}
SCHEMA_PATH = pathlib.Path(__file__).with_name('chromium-diagnostics') / 'schema.json'


def decimal(value):
    """Diagnostic integral values are decimal strings, never JSON doubles."""
    if not isinstance(value, str) or not re.fullmatch(r'-?[0-9]+', value):
        return None
    # A malformed input is not allowed to manufacture an unbounded bigint.
    if len(value) > 21:
        return None
    parsed = int(value)
    return parsed if -(2**63) <= parsed < 2**64 else None


def unsigned(value):
    parsed = decimal(value)
    return parsed if parsed is not None and parsed >= 0 else None


def signed64(value):
    parsed = decimal(value)
    return parsed if parsed is not None and parsed < 2**63 else None


def source_trace_id_state(value):
    return 'missing_or_invalid' if value is None else 'unset' if value == -1 else 'captured_signed64'


def position(timestamp, alignment):
    if alignment is None:
        return {'nominal': 'whole_trace', 'boundary_uncertain': False}
    start, end, uncertainty = (alignment[key] for key in
                              ('trace_start_us', 'trace_end_us', 'uncertainty_us'))
    return {'nominal': 'before' if timestamp < start else 'inside' if timestamp < end else 'after',
            'boundary_uncertain': abs(timestamp-start) <= uncertainty or abs(timestamp-end) <= uncertainty}


def issue(code, indices=(), **detail):
    return {'code': code, 'trace_indices': list(indices), **detail}


def instance_key(record):
    return record['pid'], record['object'], record['instance_id']


def valid_coordinate_id(value):
    return ((type(value) is int and value >= 0) or
            (isinstance(value, str) and bool(re.fullmatch(r'[0-9]+', value))))


def safe_preview(value):
    # Preview is a string so NaN in malformed input cannot poison report JSON.
    try:
        encoded = json.dumps(value, ensure_ascii=False)
    except (TypeError, ValueError):
        encoded = repr(value)
    return {'encoded_preview': encoded[:4096], 'original_characters': len(encoded)}


def parser_safe_events(events):
    safe, rejected = [], []
    for index, event in enumerate(events):
        invalid = (not isinstance(event, dict) or
                   any(not isinstance(event.get(key, ''), str) for key in ('name', 'cat', 'ph')) or
                   not isinstance(event.get('args', {}), dict))
        if invalid:
            safe.append(None)
            rejected.append({'trace_index': index, 'event': safe_preview(event)})
        else:
            safe.append(event)
    return safe, rejected


def parse_records(events, alignment):
    records, rejected, issues = [], [], []
    for index, event in enumerate(events):
        if not isinstance(event, dict) or event.get('name') != EVENT:
            continue
        args = event.get('args')
        bt = args.get('bt') if isinstance(args, dict) else None
        errors = []
        if event.get('ph') not in ('I', 'i'):
            errors.append('diagnostic_event_is_not_instant')
        if not isinstance(event.get('cat'), str) or CATEGORY not in event['cat'].split(','):
            errors.append('diagnostic_category_missing')
        if not number(event.get('ts')) or not all(valid_coordinate_id(event.get(key)) for key in ('pid', 'tid')):
            errors.append('invalid_trace_coordinates')
        if not isinstance(bt, dict):
            errors.append('missing_bt_object')
        else:
            if type(bt.get('schema_version')) is not int or bt['schema_version'] != 1:
                errors.append('unsupported_schema_version')
            if not isinstance(bt.get('object'), str) or bt['object'] not in OBJECTS:
                errors.append('invalid_object_kind')
            if not isinstance(bt.get('kind'), str) or not bt['kind']:
                errors.append('missing_record_kind')
            for field in ('instance_id', 'event_seq'):
                if unsigned(bt.get(field)) is None:
                    errors.append('invalid_' + field)
            if 'reset_epoch' in bt and unsigned(bt['reset_epoch']) is None:
                errors.append('invalid_reset_epoch')
        if errors:
            rejected.append({'trace_index': index, 'errors': errors, 'event': safe_preview(event)})
            issues.extend(issue(code, [index]) for code in errors)
            continue
        record = {'trace_index': index, 'ts_us': event['ts'], 'pid': str(event['pid']),
                  'tid': str(event['tid']), 'object': bt['object'],
                  'instance_id': str(unsigned(bt['instance_id'])),
                  'event_seq': unsigned(bt['event_seq']), 'kind': bt['kind'],
                  'reset_epoch': unsigned(bt['reset_epoch']) if 'reset_epoch' in bt else None,
                  'window': position(event['ts'], alignment), 'bt': bt}
        records.append(record)
    return records, rejected, issues


def instance_inventory(records):
    groups = collections.defaultdict(list)
    for record in records:
        groups[instance_key(record)].append(record)
    descriptions, issues = [], []
    for key, rows in sorted(groups.items()):
        rows.sort(key=lambda row: (row['event_seq'], row['trace_index']))
        duplicates, gaps, regressions = [], [], []
        for previous, current in zip(rows, rows[1:]):
            indices = [previous['trace_index'], current['trace_index']]
            if current['event_seq'] == previous['event_seq']:
                duplicates.append(indices)
                issues.append(issue('duplicate_instance_sequence', indices, instance=list(key)))
            elif current['event_seq'] != previous['event_seq']+1:
                gap = {'trace_indices': indices, 'missing_sequence_range': [previous['event_seq']+1, current['event_seq']-1]}
                gaps.append(gap)
                issues.append(issue('instance_sequence_gap', indices, instance=list(key),
                                    missing_sequence_range=gap['missing_sequence_range']))
            if current['ts_us'] < previous['ts_us']:
                regressions.append(indices)
                issues.append(issue('instance_clock_regression', indices, instance=list(key)))
        descriptions.append({'pid': key[0], 'object': key[1], 'instance_id': key[2],
            'producer_tids': sorted({row['tid'] for row in rows}),
            'event_count': len(rows), 'event_sequence_range': [rows[0]['event_seq'], rows[-1]['event_seq']],
            'first_last_timestamp_us_in_sequence_order': [rows[0]['ts_us'], rows[-1]['ts_us']],
            'duplicate_sequences': duplicates, 'sequence_gaps': gaps, 'clock_regressions': regressions,
            'trace_indices_in_sequence_order': [row['trace_index'] for row in rows]})
    return groups, descriptions, issues


def integer(record, field, issues, required=True, nonnegative=True):
    value = (unsigned if nonnegative else decimal)(record['bt'].get(field))
    if value is None and (required or field in record['bt']):
        issues.append(issue('invalid_or_missing_integer_field', [record['trace_index']], field=field))
    return value


def interval(first, last):
    if first is None or last is None or last['ts_us'] < first['ts_us']:
        return None
    return last['ts_us'] - first['ts_us']


def validate_fields(records, schema):
    issues = []
    booleans = {'sink_valid', 'sink_bound', 'bundle_valid', 'bundle_alive', 'surface_visible',
                'page_visible', 'force_submit', 'force_begin_frames', 'is_rendering',
                'last_frame_id_valid', 'output_computed', 'size_change_evaluated',
                'mirrored', 'size_changed', 'client_found', 'applied', 'submitted',
                'input_valid', 'output_valid', 'should_submit_evaluated'}
    words = {'origin', 'guard_stage', 'reason', 'phase', 'cause', 'entry_kind', 'route'}
    for record in records:
        bt, kind = record['bt'], record['kind']
        required = []
        if kind not in schema['kinds']:
            issues.append(issue('unknown_record_kind', [record['trace_index']], kind=kind))
        if record['object'] == 'submitter':
            required.extend(schema['submitter_snapshot'])
        if kind.startswith('submit.'):
            required.extend(schema['attempt_fields'])
        if kind in ('bundle.send.enter', 'bundle.receive.enter'):
            required.extend(schema['batch_fields'])
        if kind in ('bundle.send.entry', 'bundle.receive.entry', 'bundle.receive.entry_done'):
            required.extend(schema['entry_fields'])
        if kind == 'bundle.enqueue':
            required.extend(('entry_kind','queue_index','resource_count','sink_client_id','sink_id'))
            if bt.get('entry_kind') not in ('ack','reclaim','begin_frame'):
                issues.append(issue('invalid_enqueue_entry_kind', [record['trace_index']]))
        if kind.startswith('ack.'):
            required.append('callback_id')
        if kind in ('counter.change', 'counter.reset'):
            required.extend(schema['counter_fields'])
        if kind == 'counter.reset':
            required.extend(schema['counter_reset_extra'])
        if kind in ('service.admit','surface.ack.ready','surface.ack.done'):
            required.append('surface_frame_trace_id')
        elif 'surface_frame_trace_id' in bt:
            required.append('surface_frame_trace_id')
        for field in set(required):
            if field == 'surface_frame_trace_id':
                if signed64(bt.get(field)) is None:
                    issues.append(issue('invalid_or_missing_signed64_field', [record['trace_index']], field=field))
            elif field in booleans:
                if type(bt.get(field)) is not bool:
                    issues.append(issue('invalid_or_missing_boolean_field', [record['trace_index']], field=field))
            elif field in words:
                if not isinstance(bt.get(field), str) or not bt[field]:
                    issues.append(issue('invalid_or_missing_string_field', [record['trace_index']], field=field))
            else:
                integer(record, field, issues, nonnegative=(field != 'pts_us'))
        if kind in ('submit.construct', 'submit.send') and bt.get('phase') not in ('enter', 'exit'):
            issues.append(issue('invalid_submit_phase', [record['trace_index']]))
        if kind == 'submit.return' and (not isinstance(bt.get('reason'), str) or bt['reason'] not in REASONS):
            issues.append(issue('invalid_return_reason', [record['trace_index']]))
        if kind.startswith('submit.'):
            if bt.get('origin') not in schema['attempt_origins']:
                issues.append(issue('invalid_attempt_origin', [record['trace_index']]))
            if bt.get('guard_stage') not in schema['guard_stages']:
                issues.append(issue('invalid_guard_stage', [record['trace_index']]))
            if bt.get('output_computed') is True:
                for field in ('output_width', 'output_height'):
                    integer(record, field, issues)
            if bt.get('size_change_evaluated') is True and type(bt.get('size_changed')) is not bool:
                issues.append(issue('missing_evaluated_size_change', [record['trace_index']]))
    by_index = collections.defaultdict(list)
    for row in issues:
        for index in row['trace_indices']:
            by_index[index].append(row['code'])
    for record in records:
        record['field_validation_errors'] = by_index[record['trace_index']]
    return issues


def pending_history(groups):
    """Only explicit initial/reset anchors repair an incomplete count history."""
    histories, states, issues = [], {}, []
    for key, rows in sorted(groups.items()):
        if key[1] != 'submitter':
            continue
        previous_seq = None
        pending = epoch = None
        anchored = ended = begun = False
        duplicate_sequences = {seq for seq, count in collections.Counter(row['event_seq'] for row in rows).items() if count > 1}
        points = []
        for record in rows:
            index, seq, kind, bt = record['trace_index'], record['event_seq'], record['kind'], record['bt']
            observed = integer(record, 'pending_ack', issues)
            observed_epoch = record['reset_epoch']
            local = []
            if record.get('field_validation_errors'):
                local.append('malformed_record_fields')
                anchored = False
            if previous_seq is not None and seq != previous_seq+1:
                anchored = False
                local.append('sequence_discontinuity')
            if seq in duplicate_sequences:
                anchored = False
                local.append('duplicate_sequence')
            if ended:
                anchored = False
                local.append('event_after_instance_end')
            if kind == 'instance.begin':
                if begun:
                    local.append('duplicate_instance_begin')
                if observed != 0 or observed_epoch != 0:
                    local.append('nonzero_initial_state')
                anchored = not local and observed == 0 and observed_epoch == 0
                pending, epoch, begun = observed, observed_epoch, True
            elif kind == 'counter.reset':
                before = integer(record, 'pending_before', issues)
                after = integer(record, 'pending_after', issues)
                old_epoch = integer(record, 'previous_epoch', issues)
                valid = (not record.get('field_validation_errors') and bt.get('cause') in ('context_lost', 'empty_submit') and
                         old_epoch is not None and observed_epoch == old_epoch+1 and after == 0 and observed == after)
                if anchored and (before != pending or old_epoch != epoch):
                    local.append('reset_before_state_mismatch')
                if not valid:
                    local.append('invalid_counter_reset')
                # Even after a gap, a well-formed explicit reset establishes the
                # new zero state. The missing pre-reset history is still reported.
                anchored = valid and not ended and seq not in duplicate_sequences
                pending, epoch = after, observed_epoch
            else:
                if observed_epoch is None:
                    local.append('missing_epoch')
                    anchored = False
                elif epoch is not None and observed_epoch != epoch:
                    local.append('epoch_changed_without_reset')
                    anchored = False
                if kind == 'counter.change':
                    before = integer(record, 'pending_before', issues)
                    after = integer(record, 'pending_after', issues)
                    cause = bt.get('cause')
                    valid = (before is not None and after is not None and
                             ((cause == 'submit' and after == before+1) or
                              (cause == 'ack' and before > 0 and after == before-1)))
                    if not valid:
                        local.append('invalid_counter_change')
                    if anchored and before != pending:
                        local.append('counter_before_state_mismatch')
                    if observed != after:
                        local.append('counter_snapshot_after_mismatch')
                    anchored = anchored and valid and not local
                    pending = after
                elif anchored and observed != pending:
                    local.append('unrecorded_counter_change')
                    anchored = False
                else:
                    pending = observed
                epoch = observed_epoch
            if kind == 'instance.end':
                ended = True
            if observed is None:
                anchored = False
            for code in local:
                issues.append(issue(code, [index], instance=list(key)))
            point = {'trace_index': index, 'event_seq': seq, 'kind': kind,
                     'reset_epoch': epoch, 'observed_pending': observed,
                     'history_reconciled_at_record': anchored, 'issues': local}
            points.append(point)
            states[index] = point
            previous_seq = seq
        histories.append({'pid': key[0], 'instance_id': key[2], 'points': points,
                          'initial_anchor_observed': begun, 'instance_end_observed': ended})
    return histories, states, issues


def analyze_attempts(records, states):
    groups, issues = collections.defaultdict(list), []
    for record in records:
        if record['object'] == 'submitter' and (record['kind'].startswith('submit.') or
                (record['kind'] == 'counter.change' and record['bt'].get('cause') == 'submit')):
            attempt = integer(record, 'attempt_id', issues)
            if attempt is not None:
                groups[instance_key(record)+(attempt,)].append(record)
    attempts = []
    for key, rows in sorted(groups.items()):
        rows.sort(key=lambda row: (row['event_seq'], row['trace_index']))
        entries = [row for row in rows if row['kind'] == 'submit.entry']
        returns = [row for row in rows if row['kind'] == 'submit.return']
        entry, returned = (entries[0] if len(entries) == 1 else None), (returns[0] if len(returns) == 1 else None)
        local = []
        if any(row.get('field_validation_errors') for row in rows):
            local.append('malformed_record_fields')
        if len(entries) != 1:
            local.append('missing_entry' if not entries else 'duplicate_entry')
        if len(returns) != 1:
            local.append('missing_return' if not returns else 'duplicate_return')
        if entry and returned and returned['event_seq'] <= entry['event_seq']:
            local.append('return_not_after_entry')
        identities = {unsigned(row['bt'].get('unique_frame_id')) for row in rows if row['kind'].startswith('submit.')}
        entry_epochs = {unsigned(row['bt'].get('entry_epoch')) for row in rows if row['kind'].startswith('submit.')}
        if None in identities or len(identities) != 1:
            local.append('missing_or_conflicting_unique_frame_identity')
        if None in entry_epochs or len(entry_epochs) != 1:
            local.append('missing_or_conflicting_entry_epoch')
        if entry and unsigned(entry['bt'].get('entry_epoch')) != entry['reset_epoch']:
            local.append('entry_epoch_does_not_match_entry_state')
        if entry and entry['bt'].get('should_submit_evaluated') is not False:
            local.append('entry_should_submit_already_evaluated')
        for field in ('origin', 'pts_us', 'natural_width', 'natural_height', 'previous_width', 'previous_height'):
            values = [row['bt'].get(field) for row in rows if row['kind'].startswith('submit.')]
            if values and any(value != values[0] for value in values[1:]):
                local.append('attempt_constant_changed:'+field)
        if len({row['tid'] for row in rows}) != 1:
            local.append('attempt_records_cross_threads')
        epochs = {row['reset_epoch'] for row in rows}
        raw_reason = returned['bt'].get('reason') if returned else None
        reason = raw_reason if isinstance(raw_reason, str) else None
        if returned and reason not in REASONS:
            local.append('unknown_return_reason')
        phases = {}
        for kind in ('submit.construct', 'submit.send'):
            starts = [row for row in rows if row['kind'] == kind and row['bt'].get('phase') == 'enter']
            ends = [row for row in rows if row['kind'] == kind and row['bt'].get('phase') == 'exit']
            phase_issues = []
            if starts or ends:
                if len(starts) != 1 or len(ends) != 1:
                    phase_issues.append('missing_or_duplicate_phase_boundary')
                elif starts[0]['event_seq'] >= ends[0]['event_seq']:
                    phase_issues.append('phase_exit_not_after_entry')
                elif entry and returned and not (entry['event_seq'] < starts[0]['event_seq'] < ends[0]['event_seq'] < returned['event_seq']):
                    phase_issues.append('phase_outside_attempt')
            phases[kind] = {'enter_indices': [row['trace_index'] for row in starts],
                            'exit_indices': [row['trace_index'] for row in ends],
                            'elapsed_us': interval(starts[0], ends[0]) if len(starts) == len(ends) == 1 else None,
                            'issues': phase_issues}
            local.extend(kind+':'+code for code in phase_issues)
        changes = [row for row in rows if row['kind'] == 'counter.change']
        if reason == 'accepted':
            if any(len(phases[kind]['enter_indices']) != 1 or len(phases[kind]['exit_indices']) != 1 for kind in phases):
                local.append('accepted_without_complete_construct_and_send')
            if len(changes) != 1:
                local.append('accepted_without_unique_submit_counter_change')
            if (entry and returned and len(changes) == 1 and all(
                    len(phases[kind]['enter_indices']) == len(phases[kind]['exit_indices']) == 1 for kind in phases)):
                lookup = {row['trace_index']: row['event_seq'] for row in rows}
                ordered = [entry['event_seq']]
                for kind in ('submit.construct', 'submit.send'):
                    ordered.extend(lookup[phases[kind][part+'_indices'][0]] for part in ('enter', 'exit'))
                ordered.extend((changes[0]['event_seq'], returned['event_seq']))
                if any(second <= first for first, second in zip(ordered, ordered[1:])):
                    local.append('accepted_source_stage_order_inconsistent')
        elif reason in REASONS:
            if any(phases[kind]['enter_indices'] or phases[kind]['exit_indices'] for kind in phases) or changes:
                local.append('rejected_with_construction_send_or_increment')
        if returned and reason == 'pending_ack':
            bt = returned['bt']
            if (unsigned(bt.get('pending_ack')) in (None, 0) or bt.get('size_change_evaluated') is not True
                    or bt.get('size_changed') is not False or bt.get('guard_stage') != 'pending_ack'):
                local.append('pending_ack_guard_state_inconsistent')
        if returned and reason in REASONS:
            bt = returned['bt']
            if bt.get('should_submit_evaluated') is not (reason != 'no_sink'):
                local.append('should_submit_evaluation_inconsistent')
            if entry:
                initial = entry['bt']
                if reason != 'no_sink' and initial.get('sink_bound') is not True:
                    local.append('earlier_sink_guard_inconsistent_at_entry')
                if reason not in ('no_sink', 'not_visible') and not (initial.get('force_submit') is True or
                        (initial.get('surface_visible') is True and initial.get('page_visible') is True)):
                    local.append('earlier_visibility_guard_inconsistent_at_entry')
                if reason in ('empty_output', 'pending_ack', 'accepted') and initial.get('last_frame_id_valid') is True and (
                        unsigned(initial.get('last_frame_id')) == unsigned(initial.get('unique_frame_id'))):
                    local.append('earlier_duplicate_guard_inconsistent_at_entry')
            expected_stage = {'no_sink': 'sink_visibility', 'not_visible': 'sink_visibility',
                'duplicate_frame': 'duplicate', 'empty_output': 'output',
                'pending_ack': 'pending_ack', 'accepted': 'accepted'}[reason]
            if bt.get('guard_stage') != expected_stage:
                local.append('return_guard_stage_inconsistent')
            if reason == 'no_sink' and bt.get('sink_bound') is not False:
                local.append('no_sink_guard_state_inconsistent')
            if reason == 'not_visible' and (bt.get('sink_bound') is not True or bt.get('force_submit') is not False
                    or (bt.get('surface_visible') is True and bt.get('page_visible') is True)):
                local.append('visibility_guard_state_inconsistent')
            if reason == 'duplicate_frame' and (bt.get('last_frame_id_valid') is not True
                    or unsigned(bt.get('last_frame_id')) != unsigned(bt.get('unique_frame_id'))):
                local.append('duplicate_guard_state_inconsistent')
            if reason == 'empty_output' and (bt.get('output_computed') is not True or
                    0 not in (unsigned(bt.get('output_width')), unsigned(bt.get('output_height')))):
                local.append('empty_output_guard_state_inconsistent')
            if reason in ('no_sink', 'not_visible', 'duplicate_frame') and (
                    bt.get('output_computed') is not False or bt.get('size_change_evaluated') is not False):
                local.append('later_guard_fields_evaluated_before_early_return')
        if entry and returned and (entry['tid'] != returned['tid'] or interval(entry, returned) is None):
            local.append('attempt_thread_or_clock_inconsistent')
        output = {'pid': key[0], 'instance_id': key[2], 'attempt_id': key[3],
            'trace_indices': [row['trace_index'] for row in rows],
            'entry_indices': [row['trace_index'] for row in entries],
            'return_indices': [row['trace_index'] for row in returns],
            'unique_frame_id': next(iter(identities)) if len(identities) == 1 else None,
            'entry_epoch': next(iter(entry_epochs)) if len(entry_epochs) == 1 else None,
            'observed_epochs': sorted(value for value in epochs if value is not None),
            'crossed_reset_epoch': len(epochs) > 1,
            'reason': reason, 'origin': entry['bt'].get('origin') if entry else None,
            'elapsed_us': interval(entry, returned), 'phases': phases,
            'window': entry['window'] if entry else returned['window'] if returned else None,
            'entry_pending': unsigned(entry['bt'].get('pending_ack')) if entry else None,
            'return_pending': unsigned(returned['bt'].get('pending_ack')) if returned else None,
            'entry_history_reconciled': states.get(entry['trace_index'], {}).get('history_reconciled_at_record', False) if entry else False,
            'return_history_reconciled': states.get(returned['trace_index'], {}).get('history_reconciled_at_record', False) if returned else False,
            'complete_consistent_attempt': not local, 'issues': local}
        attempts.append(output)
        issues.extend(issue('attempt:'+code, output['trace_indices'], attempt_id=key[3], instance=list(key[:3])) for code in local)
    return attempts, issues


def analyze_callbacks(records, instances):
    groups, issues = collections.defaultdict(list), []
    for record in records:
        if record['object'] == 'submitter' and (record['kind'].startswith('ack.') or
                (record['kind'] == 'counter.change' and record['bt'].get('cause') == 'ack')):
            callback = integer(record, 'callback_id', issues)
            groups[instance_key(record)+(callback if callback is not None else -1-record['trace_index'],)].append(record)
    callbacks = []
    for key, rows in sorted(groups.items()):
        rows.sort(key=lambda row: (row['event_seq'], row['trace_index']))
        by_kind = {kind: [row for row in rows if row['kind'] == kind] for kind in
                   ('ack.enter', 'ack.reclaim_done', 'counter.change', 'ack.exit')}
        local = []
        if key[3] < 0:
            local.append('missing_callback_identity')
        if any(row.get('field_validation_errors') for row in rows):
            local.append('malformed_record_fields')
        # Keep every usable boundary even if another body field is malformed.
        # Incomplete callbacks remain rivals to a seemingly unique dispatch.
        enter, done, leave = (by_kind[kind][0] if len(by_kind[kind]) == 1 else None
                             for kind in ('ack.enter', 'ack.reclaim_done', 'ack.exit'))
        resource_count = unsigned(enter['bt'].get('resource_count')) if enter else None
        if enter and resource_count is None:
            local.append('missing_callback_resource_count')
        for kind in ('ack.enter', 'ack.reclaim_done', 'ack.exit'):
            if len(by_kind[kind]) != 1:
                local.append('missing_or_duplicate_'+kind)
        sink_identity = {}
        for field in ('sink_client_id','sink_id'):
            values = {unsigned(row['bt'].get(field)) for row in rows}
            sink_identity[field] = next(iter(values)) if len(values) == 1 else None
            if None in values or len(values) != 1:
                local.append('missing_or_conflicting_'+field)
        if enter and done and leave:
            if not enter['event_seq'] < done['event_seq'] < leave['event_seq']:
                local.append('callback_stage_order_invalid')
            between = [row for row in instances[key[:3]] if enter['event_seq'] <= row['event_seq'] <= leave['event_seq']]
            if any(b['event_seq'] != a['event_seq']+1 for a,b in zip(between,between[1:])):
                local.append('callback_sequence_discontinuity')
            if any(b['ts_us'] < a['ts_us'] for a,b in zip(between,between[1:])):
                local.append('callback_clock_regression')
            if len({row['tid'] for row in rows}) != 1 or not enter['ts_us'] <= done['ts_us'] <= leave['ts_us']:
                local.append('callback_thread_or_clock_inconsistent')
            applied = leave['bt'].get('applied')
            changes = by_kind['counter.change']
            if type(applied) is not bool or (applied and len(changes) != 1) or (applied is False and changes):
                local.append('applied_flag_counter_change_mismatch')
            if changes and not (done['event_seq'] < changes[0]['event_seq'] < leave['event_seq']):
                local.append('counter_change_not_after_reclaim')
            for change in changes:
                before, after = (unsigned(change['bt'].get(field)) for field in ('pending_before','pending_after'))
                if before is None or after is None or before < 1 or after != before-1 or unsigned(change['bt'].get('pending_ack')) != after:
                    local.append('ack_counter_delta_inconsistent')
            if applied is False and unsigned(leave['bt'].get('pending_ack')) != 0:
                local.append('zero_count_ack_has_nonzero_pending_snapshot')
        output = {'pid': key[0], 'instance_id': key[2], 'callback_id': key[3] if key[3] >= 0 else None,
            'trace_indices': [row['trace_index'] for row in rows],
            'enter_index': enter['trace_index'] if enter else None,
            'exit_index': leave['trace_index'] if leave else None,
            'enter_indices': [row['trace_index'] for row in by_kind['ack.enter']],
            'exit_indices': [row['trace_index'] for row in by_kind['ack.exit']],
            'producer_tids': sorted({row['tid'] for row in rows}), 'sink_identity': sink_identity,
            'resource_count': resource_count,
            'start_us': min(row['ts_us'] for row in by_kind['ack.enter']) if by_kind['ack.enter'] else None,
            'end_us': max(row['ts_us'] for row in by_kind['ack.exit']) if by_kind['ack.exit'] else None,
            'reclaim_elapsed_us': interval(enter, done), 'total_elapsed_us': interval(enter, leave),
            'applied': leave['bt'].get('applied') if leave else None,
            'epochs': sorted({row['reset_epoch'] for row in rows if row['reset_epoch'] is not None}),
            'complete_consistent_callback': not local, 'issues': local,
            'acknowledged_frame_identity': None}
        callbacks.append(output)
        issues.extend(issue('callback:'+code, output['trace_indices']) for code in local)
    return callbacks, issues


def member_signature(record):
    bt = record['bt']
    return (bt.get('entry_kind') if isinstance(bt.get('entry_kind'), str) else None, unsigned(bt.get('sink_client_id')),
            unsigned(bt.get('sink_id')), unsigned(bt.get('resource_count')))


def analyze_batches(records, groups, callbacks):
    buckets, issues = collections.defaultdict(list), []
    callback_comparison_budget = 1_500_000
    by_index = {row['trace_index']: row for row in records}
    for record in records:
        for direction in ('send', 'receive'):
            if record['kind'].startswith('bundle.'+direction+'.'):
                batch_id = integer(record, 'batch_id', issues)
                if batch_id is not None:
                    buckets[instance_key(record)+(direction, batch_id)].append(record)
    batches = []
    for key, rows in sorted(buckets.items()):
        direction, batch_id = key[-2:]
        prefix = 'bundle.'+direction+'.'
        enters = [row for row in rows if row['kind'] == prefix+'enter']
        exits = [row for row in rows if row['kind'] == prefix+'exit']
        members = [row for row in rows if row['kind'] == prefix+'entry']
        done = [row for row in rows if row['kind'] == prefix+'entry_done']
        enter = enters[0] if len(enters) == 1 else None
        leave = exits[0] if len(exits) == 1 else None
        local, member_rows = [], []
        if any(row.get('field_validation_errors') for row in rows):
            local.append('malformed_record_fields')
        if not enter or not leave:
            local.append('missing_or_duplicate_batch_boundary')
        elif enter['event_seq'] >= leave['event_seq'] or enter['tid'] != leave['tid'] or interval(enter, leave) is None:
            local.append('invalid_batch_interval')
        counts = {kind: integer(enter, field, issues) if enter else None for kind, field in
                  (('ack', 'ack_count'), ('reclaim', 'reclaim_count'), ('begin_frame', 'begin_frame_count'))}
        for kind, declared in counts.items():
            matching = [row for row in members if row['bt'].get('entry_kind') == kind]
            observed = sorted(unsigned(row['bt'].get('entry_index')) for row in matching
                              if unsigned(row['bt'].get('entry_index')) is not None)
            if declared is None or declared != len(matching) or observed != list(range(len(matching))):
                local.append('missing_duplicate_or_noncontiguous_'+kind+'_members')
        for member in members:
            bt = member['bt']
            index = unsigned(bt.get('entry_index'))
            matching_done = [row for row in done if row['bt'].get('entry_kind') == bt.get('entry_kind')
                             and unsigned(row['bt'].get('entry_index')) == index]
            found = bt.get('client_found') if direction == 'receive' else None
            member_issues = []
            if member.get('field_validation_errors') or any(row.get('field_validation_errors') for row in matching_done):
                member_issues.append('malformed_member_fields')
            if not isinstance(bt.get('entry_kind'), str) or bt['entry_kind'] not in counts or index is None:
                member_issues.append('invalid_member_kind_or_index')
            if enter and leave and not enter['event_seq'] < member['event_seq'] < leave['event_seq']:
                member_issues.append('member_outside_batch')
            if direction == 'receive':
                if type(found) is not bool or (found and len(matching_done) != 1) or (found is False and matching_done):
                    member_issues.append('client_found_dispatch_completion_mismatch')
                if len(matching_done) == 1 and matching_done[0]['event_seq'] <= member['event_seq']:
                    member_issues.append('member_completion_not_after_dispatch')
                if len(matching_done) == 1 and (member['tid'] != matching_done[0]['tid'] or
                        member['ts_us'] > matching_done[0]['ts_us'] or
                        member_signature(member) != member_signature(matching_done[0])):
                    member_issues.append('member_completion_thread_clock_or_identity_mismatch')
            callback_candidates = []
            boundary_candidates = []
            ambiguous_callbacks = []
            if direction == 'receive' and bt.get('entry_kind') == 'ack' and found is True and len(matching_done) == 1:
                finish = matching_done[0]
                for callback in callbacks:
                    callback_comparison_budget -= 1
                    if callback_comparison_budget < 0:
                        raise ValueError('callback matching comparison budget exceeded')
                    if callback['pid'] != member['pid'] or member['tid'] not in callback['producer_tids']:
                        continue
                    if any(callback['sink_identity'][field] is not None and member_signature(member)[position] is not None and
                           callback['sink_identity'][field] != member_signature(member)[position]
                           for field,position in (('sink_client_id',1),('sink_id',2))):
                        continue
                    if callback['resource_count'] is not None and member_signature(member)[3] is not None and callback['resource_count'] != member_signature(member)[3]:
                        continue
                    first, last = callback['start_us'], callback['end_us']
                    low, high = (first if first is not None else float('-inf'), last if last is not None else float('inf'))
                    low, high = min(low,high), max(low,high)
                    if not callback['complete_consistent_callback']:
                        if low <= finish['ts_us'] and high >= member['ts_us']:
                            ambiguous_callbacks.append(callback['trace_indices'])
                    elif member['ts_us'] < first <= last < finish['ts_us']:
                        callback_candidates.append(callback['enter_index'])
                    elif member['ts_us'] <= first <= last <= finish['ts_us']:
                        boundary_candidates.append(callback['enter_index'])
            linked = (callback_candidates[0] if len(callback_candidates) == 1 and not boundary_candidates and
                      not ambiguous_callbacks and not member_issues else None)
            member_rows.append({'trace_index': member['trace_index'], 'entry_kind': member_signature(member)[0],
                'entry_index': index, 'signature': list(member_signature(member)),
                'client_found': found, 'completion_indices': [row['trace_index'] for row in matching_done],
                'callback_candidate_enter_indices': callback_candidates,
                'callback_boundary_ambiguous_enter_indices': boundary_candidates,
                'callback_incomplete_or_invalid_rival_trace_indices': ambiguous_callbacks,
                'uniquely_nested_callback_enter_index': linked,
                'callback_link_basis': 'strict same-thread dispatch interval and sink identity; no protocol frame ID',
                'issues': member_issues})
            local.extend('member:'+value for value in member_issues)
        matched_done = {index for row in member_rows for index in row['completion_indices']}
        if any(row['trace_index'] not in matched_done for row in done):
            local.append('orphan_entry_completion')
        instance_rows = groups.get(key[:3], [])
        between = [row for row in instance_rows if enter and leave and enter['event_seq'] <= row['event_seq'] <= leave['event_seq']]
        contiguous = bool(enter and leave and between and all(b['event_seq'] == a['event_seq']+1 for a,b in zip(between, between[1:])))
        if not contiguous:
            local.append('instance_sequence_gap_or_duplicate_within_batch')
        if any(b['ts_us'] < a['ts_us'] for a,b in zip(between, between[1:])):
            local.append('instance_clock_regression_within_batch')
        output = {'pid': key[0], 'object': key[1], 'instance_id': key[2],
            'direction': direction, 'local_batch_id': batch_id,
            'enter_index': enter['trace_index'] if enter else None, 'exit_index': leave['trace_index'] if leave else None,
            'trace_indices': [row['trace_index'] for row in sorted(rows, key=lambda row: row['event_seq'])],
            'declared_counts': counts, 'members': member_rows, 'elapsed_us': interval(enter, leave),
            'complete_consistent_batch': not local, 'issues': local,
            'instance_sequence_contiguous_within_batch': contiguous,
            'instance_begin_observed_before_batch': any(row['kind'] == 'instance.begin' and enter and row['event_seq'] < enter['event_seq'] for row in instance_rows),
            'local_queue_membership': 'not_applicable' if direction == 'receive' else 'unresolved'}
        batches.append(output)
        issues.extend(issue('batch:'+value, output['trace_indices']) for value in local)
    dispatch_owners = collections.Counter(index for batch in batches for member in batch['members']
        for index in member['callback_candidate_enter_indices']+member['callback_boundary_ambiguous_enter_indices'])
    for batch in batches:
        for member in batch['members']:
            linked = member['uniquely_nested_callback_enter_index']
            if linked is not None and dispatch_owners[linked] != 1:
                member['uniquely_nested_callback_enter_index'] = None
                member['issues'].append('callback_matches_multiple_dispatches')
                issues.append(issue('callback_matches_multiple_dispatches', [member['trace_index'], linked]))
    # A sender's enqueue arrays are independently checked against each flush.
    # Index/signature equality provides a local FIFO observation only; it never
    # joins two processes or invents a per-frame protocol ACK identifier.
    send_by_enter = {row['enter_index']: row for row in batches if row['direction'] == 'send' and row['enter_index'] is not None}
    for key, rows in groups.items():
        queues = {'ack': [], 'reclaim': [], 'begin_frame': []}
        anchored, previous_seq = False, None
        for record in rows:
            if previous_seq is not None and record['event_seq'] != previous_seq+1:
                anchored = False
            if record['kind'] == 'instance.begin':
                anchored = previous_seq is None
            elif record['kind'] == 'bundle.enqueue':
                kind = record['bt'].get('entry_kind') if isinstance(record['bt'].get('entry_kind'), str) else None
                index = integer(record, 'queue_index', issues)
                if kind not in queues or index != len(queues.get(kind, [])):
                    anchored = False
                    issues.append(issue('enqueue_queue_index_mismatch', [record['trace_index']]))
                if kind in queues:
                    queues[kind].append(record)
            if record['trace_index'] in send_by_enter:
                batch = send_by_enter[record['trace_index']]
                matching = batch['complete_consistent_batch']
                for kind in queues:
                    members = sorted((row for row in batch['members'] if row['entry_kind'] == kind), key=lambda row: row['entry_index'])
                    matching = matching and [list(member_signature(row)) for row in queues[kind]] == [row['signature'] for row in members]
                batch['enqueue_indices'] = [row['trace_index'] for queue in queues.values() for row in queue]
                batch['local_queue_membership'] = ('validated_local_fifo_membership' if matching and anchored else
                    'matching_observed_unanchored_queue' if matching else 'queue_membership_mismatch')
                for member in batch['members']:
                    member['enqueue_index'] = (queues[member['entry_kind']][member['entry_index']]['trace_index']
                        if matching and anchored else None)
                if not matching:
                    issues.append(issue('batch_queue_membership_mismatch', batch['trace_indices']))
                queues = {'ack': [], 'reclaim': [], 'begin_frame': []}
            previous_seq = record['event_seq']
    return batches, issues


def analyze_service_and_frames(records):
    service, frame_edges, frame_groups, issues = [], [], collections.defaultdict(list), []
    for record in records:
        bt, kind = record['bt'], record['kind']
        if kind.startswith('service.') or kind.startswith('surface.ack.'):
            source_trace_id = signed64(bt.get('surface_frame_trace_id'))
            row = {'trace_index': record['trace_index'], 'kind': kind,
                   'pid': record['pid'], 'object': record['object'], 'instance_id': record['instance_id'],
                   'callback_id': unsigned(bt.get('callback_id')), 'acknowledged_frame_identity': None,
                   'surface_frame_trace_id': source_trace_id,
                   'surface_frame_trace_id_state': source_trace_id_state(source_trace_id),
                   'relationship': 'local source observation; ready-to-emission and cross-process association unproven'}
            if kind in ('service.admit', 'service.ack.emit'):
                before, after = integer(record, 'pending_before', issues), integer(record, 'pending_after', issues)
                expected = 1 if kind == 'service.admit' else -1
                row['counter_delta_valid'] = before is not None and after is not None and after-before == expected
                if not row['counter_delta_valid']:
                    issues.append(issue('invalid_service_counter_delta', [record['trace_index']]))
            service.append(row)
        if kind in ('frame.prepare.begin', 'frame.select'):
            identity = integer(record, 'unique_frame_id', issues)
            if identity is not None:
                frame_groups[(record['pid'], identity)].append({'trace_index': record['trace_index'], 'relationship': kind})
        elif kind == 'frame.prepare.end':
            first = integer(record, 'input_unique_frame_id', issues) if bt.get('input_valid') is True else None
            last = integer(record, 'output_unique_frame_id', issues) if bt.get('output_valid') is True else None
            frame_edges.append({'trace_index': record['trace_index'], 'pid': record['pid'],
                'input_unique_frame_id': first, 'output_unique_frame_id': last,
                'input_valid': bt.get('input_valid'), 'output_valid': bt.get('output_valid'),
                'relationship': 'explicit preparation input/output; identities remain distinct'})
            for identity, relationship in ((first, 'preparation_input'), (last, 'preparation_output')):
                if identity is not None:
                    frame_groups[(record['pid'], identity)].append({'trace_index': record['trace_index'], 'relationship': relationship})
        elif kind == 'submit.entry':
            identity = unsigned(bt.get('unique_frame_id'))
            if identity is not None:
                frame_groups[(record['pid'], identity)].append({'trace_index': record['trace_index'], 'relationship': 'submit.entry'})
    frames = [{'pid': key[0], 'unique_frame_id': key[1], 'observations': rows,
               'identity_scope': 'same retained process lifetime; no PTS-based join'} for key, rows in sorted(frame_groups.items())]
    return service, frames, frame_edges, issues


def source_ack_calls(records, groups):
    """Correlate only explicit source call brackets; incomplete rivals persist."""
    buckets, issues = collections.defaultdict(list), []
    names = {'service': ('service.ack.emit', 'service.ack.done'),
             'surface': ('surface.ack.ready', 'surface.ack.done')}
    for record in records:
        for role, kinds in names.items():
            if record['kind'] in kinds:
                callback = integer(record, 'callback_id', issues)
                # An unidentified opening/closing is still a possible rival to
                # a complete nearby call, not evidence that may be discarded.
                buckets[(role,)+instance_key(record)+(callback if callback is not None else -1-record['trace_index'],)].append(record)
    calls = []
    for key, rows in sorted(buckets.items()):
        role = key[0]
        starts = [row for row in rows if row['kind'] == names[role][0]]
        ends = [row for row in rows if row['kind'] == names[role][1]]
        first = starts[0] if len(starts) == 1 else None
        last = ends[0] if len(ends) == 1 else None
        local = []
        if key[4] < 0:
            local.append('missing_callback_identity')
        if not first or not last:
            local.append('missing_or_duplicate_call_boundary')
        elif first['event_seq'] >= last['event_seq'] or first['tid'] != last['tid'] or interval(first,last) is None:
            local.append('invalid_call_order_or_thread')
        if any(row.get('field_validation_errors') for row in rows):
            local.append('malformed_record_fields')
        identity_fields = ('sink_client_id','sink_id','surface_client_address') if role == 'service' else (
            'client_address','frame_token','surface_frame_trace_id','frame_index')
        identities = {}
        for field in identity_fields:
            decode = signed64 if field == 'surface_frame_trace_id' else unsigned
            values = {decode(row['bt'].get(field)) for row in rows}
            if None in values or len(values) != 1:
                local.append('missing_or_conflicting_'+field)
            identities[field] = next(iter(values)) if len(values) == 1 else None
        between = [row for row in groups[instance_key(rows[0])] if first and last and
                   first['event_seq'] <= row['event_seq'] <= last['event_seq']]
        if first and last and any(b['event_seq'] != a['event_seq']+1 for a,b in zip(between,between[1:])):
            local.append('sequence_discontinuity_inside_call')
        if any(b['ts_us'] < a['ts_us'] for a,b in zip(between,between[1:])):
            local.append('clock_regression_inside_call')
        if role == 'service':
            before = integer(first, 'pending_before', issues) if first else None
            after = integer(first, 'pending_after', issues) if first else None
            if first and (before is None or after is None or before < 1 or after != before-1
                          or unsigned(first['bt'].get('service_pending')) != after):
                local.append('service_emit_pending_state_inconsistent')
            if first and first['bt'].get('route') not in ('ack','reclaim','no_client'):
                local.append('invalid_service_route')
            if first and unsigned(first['bt'].get('resource_count')) is None:
                local.append('missing_service_resource_count')
            expected = after
            for row in between[1:]:
                if row['kind'] in ('service.admit','service.ack.emit'):
                    old, new = unsigned(row['bt'].get('pending_before')), unsigned(row['bt'].get('pending_after'))
                    delta = 1 if row['kind'] == 'service.admit' else -1
                    if old != expected or old is None or new is None or new-old != delta:
                        local.append('intervening_service_pending_change_inconsistent')
                    expected = new
                if unsigned(row['bt'].get('service_pending')) != expected:
                    local.append('service_pending_snapshot_inconsistent')
        else:
            for field in ('client_present','called'):
                values = [row['bt'].get(field) for row in rows]
                if any(type(value) is not bool for value in values) or any(value != values[0] for value in values):
                    local.append('missing_or_conflicting_'+field)
            if first and first['bt'].get('client_present') != first['bt'].get('called'):
                local.append('surface_client_call_flag_inconsistent')
        call = {'id': len(calls), 'role': role, 'pid': key[1], 'object': key[2], 'instance_id': key[3],
            'callback_id': key[4] if key[4] >= 0 else None, 'trace_indices': [row['trace_index'] for row in rows],
            'enter_index': first['trace_index'] if first else None, 'exit_index': last['trace_index'] if last else None,
            'enter_indices': [row['trace_index'] for row in starts], 'exit_indices': [row['trace_index'] for row in ends],
            'producer_tids': sorted({row['tid'] for row in rows}), 'identities': identities,
            'start_us': min(row['ts_us'] for row in starts) if starts else None,
            'end_us': max(row['ts_us'] for row in ends) if ends else None,
            'elapsed_us': interval(first,last), 'route': first['bt'].get('route') if first else None,
            'resource_count': unsigned(first['bt'].get('resource_count')) if first else None,
            'called': first['bt'].get('called') if first else None,
            'complete_consistent_call': not local, 'issues': local,
            'surface_frame_trace_id_state': source_trace_id_state(identities.get('surface_frame_trace_id')),
            'protocol_ack_frame_id': None}
        calls.append(call)
        issues.extend(issue('source_call:'+code, call['trace_indices']) for code in local)
    by_index = {row['trace_index']: row for row in records}
    budget = [1_500_000]
    def indices_for(role):
        table = collections.defaultdict(list)
        for call in calls:
            if call['role'] == role:
                if role == 'surface' and call['complete_consistent_call'] and call['called'] is False:
                    continue
                for tid in call['producer_tids']:
                    low = call['start_us'] if call['start_us'] is not None else float('-inf')
                    high = call['end_us'] if call['end_us'] is not None else float('inf')
                    table[(call['pid'],tid)].append((min(low,high),max(low,high),call['id']))
        return {key:chromium_mojo_flow.IntervalIndex(rows) for key,rows in table.items()}
    service_index, surface_index = indices_for('service'), indices_for('surface')
    queue_links = []
    for record in records:
        if record['kind'] != 'bundle.enqueue':
            continue
        index = service_index.get((record['pid'],record['tid']))
        candidates = index.enclosing(record['ts_us'],record['ts_us'],budget) if index else []
        compatible = []
        for _,_,call_id in candidates:
            call = calls[call_id]
            known = call['identities']
            signature = member_signature(record)
            if any(known[field] is not None and signature[position] is not None and known[field] != signature[position] for field,position in
                   (('sink_client_id',1),('sink_id',2))):
                continue
            if call['route'] in ('ack','reclaim','no_client') and signature[0] in ('ack','reclaim','begin_frame') and call['route'] != signature[0]:
                continue
            if call['resource_count'] is not None and signature[3] is not None and call['resource_count'] != signature[3]:
                continue
            compatible.append(call_id)
        accepted = (not record.get('field_validation_errors') and len(compatible) == 1 and calls[compatible[0]]['complete_consistent_call'] and
                    calls[compatible[0]]['start_us'] < record['ts_us'] < calls[compatible[0]]['end_us'])
        queue_links.append({'enqueue_index': record['trace_index'], 'service_call_candidates': compatible,
            'accepted': accepted, 'service_call_id': compatible[0] if accepted else None,
            'basis': 'same-thread strict service callback interval, sink, route and resource count',
            'status': 'unique_source_call' if accepted else 'missing_ambiguous_or_incomplete_source_call'})
    owners = collections.Counter(candidate for row in queue_links for candidate in row['service_call_candidates'])
    for row in queue_links:
        if row['accepted'] and owners[row['service_call_id']] != 1:
            row.update(accepted=False, service_call_id=None, status='service_call_matches_multiple_enqueues')

    surface_children = collections.defaultdict(list)
    for child in calls:
        if child['role'] != 'service':
            continue
        observed_start = child['start_us'] if child['start_us'] is not None else child['end_us']
        observed_end = child['end_us'] if child['end_us'] is not None else child['start_us']
        for tid in child['producer_tids']:
            index = surface_index.get((child['pid'],tid))
            candidates = index.enclosing(min(observed_start,observed_end),max(observed_start,observed_end),budget) if index else []
            for _,_,parent_id in candidates:
                parent = calls[parent_id]
                address = parent['identities']['client_address']
                child_address = child['identities']['surface_client_address']
                if address is not None and child_address is not None and address != child_address:
                    continue
                surface_children[parent_id].append(child['id'])
    surface_links = []
    for parent in calls:
        if parent['role'] != 'surface':
            continue
        candidates = sorted(set(surface_children[parent['id']]))
        ready = [row for row in records if row['kind'] == 'service.ack.ready' and row['pid'] == parent['pid']
            and row['tid'] in parent['producer_tids'] and
            (parent['start_us'] is None or parent['start_us'] <= row['ts_us']) and
            (parent['end_us'] is None or row['ts_us'] <= parent['end_us']) and
            (parent['identities']['client_address'] is None or unsigned(row['bt'].get('surface_client_address')) in
             (None,parent['identities']['client_address']))]
        accepted = False
        if parent['complete_consistent_call'] and parent['called'] is True and len(candidates) == len(ready) == 1:
            child, entry = calls[candidates[0]], ready[0]
            accepted = (child['complete_consistent_call'] and
                (entry['object'],entry['instance_id']) == (child['object'],child['instance_id']) and
                entry['event_seq'] < by_index[child['enter_index']]['event_seq'] and
                parent['identities']['client_address'] == child['identities']['surface_client_address'] == unsigned(entry['bt'].get('surface_client_address')) and
                parent['start_us'] < entry['ts_us'] < child['start_us'] <= child['end_us'] < parent['end_us'])
        surface_links.append({'surface_call_id': parent['id'], 'service_call_candidates': candidates,
            'service_ready_indices': [row['trace_index'] for row in ready],
            'accepted': accepted, 'service_call_id': candidates[0] if accepted else None,
            'status': 'unique_local_surface_to_service_call' if accepted else
                      'null_client_no_call_expected' if parent['complete_consistent_call'] and parent['called'] is False and not candidates and not ready else
                      'missing_ambiguous_or_incomplete_source_call',
            'basis': 'strict same-thread source call brackets and SurfaceClient address during identified instance lifetime; no new wire ID'})
    owners = collections.Counter(candidate for row in surface_links for candidate in row['service_call_candidates'])
    for row in surface_links:
        if row['accepted'] and owners[row['service_call_id']] != 1:
            row.update(accepted=False, service_call_id=None, status='service_call_matches_multiple_surface_calls')
    return calls, surface_links, queue_links, issues


def required_producer_roles(records):
    roles = {'submitter': {'submitter'}, 'client_bundle': {'client_bundle'},
        'service_bundle': {'service_bundle','service_group'}, 'service_sink': {'service_sink'},
        'surface_readiness': {'surface_ack'}, 'preparation': {'preparation'}, 'selection': {'selection'}}
    observed = {row['object'] for row in records}
    return {role: {'accepted_objects': sorted(objects), 'observed_objects': sorted(objects & observed),
                   'present': bool(objects & observed)} for role,objects in roles.items()}


def analyze(events, metadata, alignment=None, schema=None):
    if schema is None:
        schema, _ = read_json(SCHEMA_PATH, 65536)
    safe_events, envelope_rejections = parser_safe_events(events)
    inv = inventory.analyze(safe_events, metadata, alignment)
    if inv['trace_metadata_evidence'].get('clock-domain') != 'LINUX_CLOCK_MONOTONIC':
        raise ValueError('requires explicit LINUX_CLOCK_MONOTONIC metadata')
    records, rejected, issues = parse_records(events, alignment)
    issues.extend(issue('shared_parser_envelope_rejected', [row['trace_index']]) for row in envelope_rejections)
    groups, instances, additions = instance_inventory(records)
    issues.extend(additions)
    issues.extend(validate_fields(records, schema))
    histories, states, additions = pending_history(groups)
    issues.extend(additions)
    attempts, additions = analyze_attempts(records, states)
    issues.extend(additions)
    callbacks, additions = analyze_callbacks(records, groups)
    issues.extend(additions)
    batches, additions = analyze_batches(records, groups, callbacks)
    issues.extend(additions)
    service, frames, frame_edges, additions = analyze_service_and_frames(records)
    issues.extend(additions)
    calls, surface_links, enqueue_links, additions = source_ack_calls(records, groups)
    issues.extend(additions)
    # The Mojo parser must see every original coordinate, including malformed
    # alternatives that poison a seemingly unique valid edge or node.
    transport = chromium_mojo_flow.link_batches(events, batches)
    flows = transport['links']
    issues.extend(issue('transport:'+row['reason'], batch_index=row['batch_index']) for row in transport['batch_envelope_issues'])
    if not transport['accepted_count']:
        issues.append(issue('cross_process_ack_delivery_not_linked'))
    roles = required_producer_roles(records)
    absent_roles = sorted(role for role,value in roles.items() if not value['present'])
    if not records:
        issues.append(issue('missing_diagnostic_records'))
    if absent_roles:
        issues.append(issue('absent_required_object_roles', roles=absent_roles))
    by_reason = collections.defaultdict(list)
    for attempt in attempts:
        by_reason[attempt['reason'] or 'unresolved'].append(attempt)
    comparisons = {reason: {'observed_attempts': len(rows),
        'complete_consistent_attempts': sum(row['complete_consistent_attempt'] for row in rows),
        'return_history_reconciled': sum(row['return_history_reconciled'] for row in rows),
        'elapsed_us': inventory.dist([row['elapsed_us'] for row in rows if row['complete_consistent_attempt'] and row['elapsed_us'] is not None]),
        'entry_pending': inventory.dist([row['entry_pending'] for row in rows if row['entry_pending'] is not None]),
        'return_pending': inventory.dist([row['return_pending'] for row in rows if row['return_pending'] is not None])}
        for reason, rows in sorted(by_reason.items())}
    return {'schema': 'chromium-submission-diagnostics-analysis-v1',
        'diagnostic_schema_version': schema['schema_version'], 'alignment': alignment,
        'summary': {'accepted_diagnostic_records': len(records), 'rejected_diagnostic_records': len(rejected),
            'missing_diagnostic_records': not records, 'absent_required_object_roles': absent_roles,
            'instances': len(instances), 'attempts': len(attempts), 'ack_callbacks': len(callbacks),
            'batches': len(batches), 'validated_mojo_batch_deliveries': sum(row['accepted'] for row in flows),
            'issue_counts': dict(collections.Counter(row['code'] for row in issues))},
        'records': records, 'rejected_records': rejected, 'instances': instances,
        'pending_histories': histories, 'attempts': attempts, 'attempt_comparison_by_return_reason': comparisons,
        'ack_callbacks': callbacks, 'batches': batches, 'flow_links': flows,
        'transport_reconstruction': {key: value for key, value in transport.items() if key != 'links'},
        'service_ack_observations': service, 'frame_identity_observations': frames,
        'source_ack_calls': calls, 'surface_to_service_links': surface_links, 'service_to_enqueue_links': enqueue_links,
        'preparation_identity_edges': frame_edges, 'issues': issues,
        'coverage': {'trace_event_count': inv['trace_event_count'],
            'trace_metadata_evidence': inv['trace_metadata_evidence'], 'inventory_coverage': inv['coverage'],
            'explicit_loss_markers': inv['explicit_loss_markers'], 'explicit_loss_markers_total': inv['explicit_loss_markers_total'],
            'pairing': inv['pairing'], 'thread_roles': inv['thread_roles'],
            'envelopes_rejected_by_shared_parsers': envelope_rejections,
            'required_object_roles': roles,
            'complete_required_producer_coverage_proven': False},
        'limits': [
            'An explicit return reason identifies the observed guard; missing state history still prevents reconstructing why that state arose.',
            'Process IDs need external executable/task-lifetime receipts. Observation instance and batch IDs are not protocol frame IDs.',
            'Counter-history reconciliation requires initial/reset anchors and uninterrupted instance sequences; observed snapshots alone do not repair gaps.',
            'Generic bundles, equal local batch IDs, PTS and returned resource counts never supply ACK-to-frame identity.',
            'Application submit.send can enqueue a bundle; bundle.send brackets a proxy call, not a completed kernel transport write.',
            'Mojo linkage is reconstructed from actual exported edges and strict diagnostic envelopes; it still requires independent capture coverage.',
            'Service/surface readiness, service ACK emission and receiver callback records remain separate without supported unique call/flow relationships.',
            'Named timestamps and source observations prove neither presentation success nor physical scanout.',
            'The signed surface_frame_trace_id value -1 is unset; equality is only a captured callback scalar and never establishes frame or flow identity.',
            'Durations include instrumentation, scheduling and nested work; off/on controls and buffer-loss review remain required.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace')
    parser.add_argument('--trace-manifest', action='store_true')
    parser.add_argument('--alignment')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    resource_limits()
    trace, identity = load_trace(args.trace, manifest=args.trace_manifest)
    alignment, alignment_identity = read_json(args.alignment, 1024*1024) if args.alignment else (None, None)
    schema, schema_identity = read_json(SCHEMA_PATH, 65536)
    events = trace_events(trace)
    metadata = {key: value for key, value in trace.items() if key != 'traceEvents'} if isinstance(trace, dict) else {}
    result = analyze(events, metadata, alignment, schema)
    result.update(input=identity, alignment_input=alignment_identity,
                  diagnostic_schema_input=schema_identity,
                  parser_inputs=parser_identity(__file__))
    result['parser_inputs'].update(inventory.parser_identity(inventory.__file__))
    result['parser_inputs'].update(chromium_mojo_flow.parser_identity(chromium_mojo_flow.__file__))
    output = write_json(args.output, result)
    print(json.dumps({'output': output, 'summary': result['summary']}))


if __name__ == '__main__':
    main()
