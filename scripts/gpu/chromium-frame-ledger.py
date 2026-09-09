#!/usr/bin/env python3
"""Enumerate observed video-stage cohorts; never assign a drop-counter residual.

Usage: chromium-frame-ledger.py TRACE [--trace-manifest] --alignment JSON
       --output FRESH.json

TRACE and alignment must be explicit, finalized repository evidence. The existing
bounded loader and strict span parser are reused. PID/PTS buckets are candidate
joins, not stream/frame identity. All occurrences remain distinct, including
unclosed scopes, missing feedback and events outside the measurement window.
"""
import argparse
import bisect
import collections
import importlib.util
import json
import pathlib
import re

from chromium_trace_common import (load_trace, number, parser_identity, read_json,
                                    resource_limits, trace_events, write_json)

spec = importlib.util.spec_from_file_location(
    'ledger_inventory', pathlib.Path(__file__).with_name('chromium-trace-inventory.py'))
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)

SCHEMA = 'chromium-frame-cohort-ledger-v1'
STAGES = {
    'VideoDecoderStream::PrepareOutput': 'preparation',
    'VideoFrameCompositor::SetCurrentFrame': 'selection',
    'VideoFrameSubmitter::SubmitFrame': 'attempt',
    'VideoFrameResourceProvider::AppendQuads': 'construction',
    'Pre-submit buffering': 'prebuffer',
    'VideoFrameSubmitter': 'reported_presentation',
}


def frame_pts(args):
    """Reject conflicting or malformed PTS; absence never becomes a shared ID."""
    candidates = []
    invalid = False
    if 'timestamp_us' in args:
        value = args['timestamp_us']
        if number(value) and int(value) == value:
            candidates.append(int(value))
        elif isinstance(value, str) and re.fullmatch(r'-?\d+', value):
            candidates.append(int(value))
        else:
            invalid = True
    frame = args.get('frame')
    if isinstance(frame, str):
        candidates.extend(int(value) for value in re.findall(r'\btimestamp:(-?\d+)\b', frame))
    unique = sorted(set(candidates))
    status = ('invalid' if invalid else 'conflicting' if len(unique) > 1
              else 'valid' if unique else 'missing')
    return {'pts_us': unique[0] if status == 'valid' else None,
            'pts_status': status, 'pts_candidates_us': unique,
            'pts_source': 'event_arguments' if status == 'valid' else None}


def window_position(ts, alignment):
    if not number(ts):
        return {'nominal': 'unknown', 'boundary_uncertain': True}
    start, end, uncertainty = (alignment[k] for k in
                              ('trace_start_us', 'trace_end_us', 'uncertainty_us'))
    return {'nominal': 'before' if ts < start else 'inside' if ts < end else 'after',
            'boundary_uncertain': (abs(ts-start) <= uncertainty or
                                   abs(ts-end) <= uncertainty)}


def occurrence(event, state, alignment):
    start, end = event.get('ts'), event.get('end')
    args = event.get('args', {})
    if not isinstance(args, dict):
        args = {}
    record = {'id': 'event-' + str(event['index']), 'stage': STAGES[event['name']],
              'name': event['name'], 'pid': str(event.get('pid', '?')),
              'tid': str(event.get('tid', '?')), 'trace_index': event['index'],
              'end_trace_index': event.get('end_index'), 'start_us': start,
              'end_us': end, 'scope_state': state, 'pairing': event.get('pairing'),
              'start_window': window_position(start, alignment),
              'end_window': window_position(end, alignment),
              **frame_pts(args)}
    record['measurement_boundary_crossing'] = bool(
        number(start) and number(end) and any(start < boundary < end for boundary in
            (alignment['trace_start_us'], alignment['trace_end_us'])))
    record['trace_censored_or_invalid_scope'] = state != 'closed'
    return record


class ContainingAttempts:
    """Bounded same-thread containment; preserve all ambiguous candidates."""
    def __init__(self, attempts):
        self.groups = {}
        buckets = collections.defaultdict(list)
        for attempt in attempts:
            if attempt['scope_state'] == 'closed':
                buckets[(attempt['pid'], attempt['tid'])].append(attempt)
        for producer, rows in buckets.items():
            rows.sort(key=lambda row: (row['start_us'], row['trace_index']))
            maximum = float('-inf')
            prefix = []
            for row in rows:
                maximum = max(maximum, row['end_us'])
                prefix.append(maximum)
            self.groups[producer] = (rows, [row['start_us'] for row in rows], prefix)

    def match(self, record, point=False):
        if record['scope_state'] != 'closed':
            return []
        rows, starts, prefix = self.groups.get((record['pid'], record['tid']), ([], [], []))
        start = record['end_us'] if point else record['start_us']
        end = record['end_us']
        position = bisect.bisect_right(starts, start)-1
        matches = []
        while position >= 0 and prefix[position] >= end:
            attempt = rows[position]
            if attempt['end_us'] >= end:
                matches.append(attempt['id'])
            position -= 1
        return sorted(matches)


def async_origin(event):
    """Exact ID/scope/producer and begin timestamp; no guessed feedback join."""
    id2 = event.get('id2')
    if isinstance(id2, dict) and ('local' in id2 or 'global' in id2):
        identity = ('id2', json.dumps(id2, sort_keys=True, separators=(',', ':')))
    elif event.get('id') is not None:
        identity = ('id', json.dumps(event['id'], sort_keys=True))
    else:
        return None
    return (str(event['pid']), event.get('cat'), event.get('scope'),
            identity, event['ts'])


def nonzero_loss_stats(value, path=()):
    result = []
    if isinstance(value, dict):
        for key, child in value.items():
            result.extend(nonzero_loss_stats(child, path+(str(key),)))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            result.extend(nonzero_loss_stats(child, path+(str(index),)))
    elif number(value) and value and re.search(
            r'loss|lost|failure|error|skipped|dropped|overwrit|wrap|overrun', '.'.join(path), re.I):
        result.append({'path': list(path), 'value': value})
    return result


def analyze(events, metadata, alignment):
    timeline = {}
    inv = inventory.analyze(events, metadata, alignment, timeline)
    if alignment is None:
        raise ValueError('a validated alignment is required')
    if inv['trace_metadata_evidence'].get('clock-domain') != 'LINUX_CLOCK_MONOTONIC':
        raise ValueError('requires explicit LINUX_CLOCK_MONOTONIC metadata')
    records, source = [], {}
    closed_begins, closed_ends = set(), set()
    for event in timeline['spans']:
        if event['name'] in STAGES:
            record = occurrence(event, 'closed', alignment)
            records.append(record)
            source[record['id']] = event
            closed_begins.add(event['index'])
            closed_ends.add(event['end_index'])
    # The inventory previews only 200 pending records. Inspect every explicitly
    # named raw stage event against valid paired spans so no target opening is
    # silently lost when that preview is truncated. A malformed pair is not
    # called merely pending: both remain unclosed/unlinked observations.
    for index, event in enumerate(events):
        if not isinstance(event, dict) or event.get('name') not in STAGES:
            continue
        phase = event.get('ph')
        if index in closed_begins or index in closed_ends:
            continue
        state = ('no_valid_closed_scope' if phase in ('B', 'b', 'S')
                 else 'no_valid_begin_link' if phase in ('E', 'e', 'F')
                 else 'invalid_complete_scope' if phase == 'X' else 'unsupported_phase')
        item = dict(event, index=index)
        record = occurrence(item, state, alignment)
        records.append(record)
        source[record['id']] = item
    records.sort(key=lambda record: record['trace_index'])
    by_id = {record['id']: record for record in records}
    stages = collections.defaultdict(list)
    for record in records:
        stages[record['stage']].append(record)

    attempts = ContainingAttempts(stages['attempt'])
    for stage in ('construction', 'prebuffer'):
        for record in stages[stage]:
            matches = attempts.match(record, point=(stage == 'prebuffer'))
            record['attempt_ids'] = matches
            record['attempt_link'] = 'unique' if len(matches) == 1 else 'ambiguous' if matches else 'missing'
            if len(matches) == 1:
                parent = by_id[matches[0]]
                parent.setdefault(stage+'_ids', []).append(record['id'])
                if record['pts_status'] == 'missing' and parent['pts_status'] == 'valid':
                    record.update(pts_us=parent['pts_us'], pts_status='valid',
                                  pts_candidates_us=[parent['pts_us']], pts_source='unique_containing_attempt')
                elif (record['pts_status'] == 'valid' and parent['pts_status'] == 'valid'
                      and record['pts_us'] != parent['pts_us']):
                    record['conflicting_attempt_pts_us'] = parent['pts_us']
    for attempt in stages['attempt']:
        append = attempt.setdefault('construction_ids', [])
        prebuffer = attempt.setdefault('prebuffer_ids', [])
        attempt['construction_markers'] = ('both' if append and prebuffer else
            'append_only' if append else 'prebuffer_only' if prebuffer else 'none_observed')

    feedback = collections.defaultdict(list)
    for record in stages['reported_presentation']:
        origin = async_origin(source[record['id']])
        if origin is not None:
            feedback[origin].append(record['id'])
        record['prebuffer_ids'] = []
    for prebuffer in stages['prebuffer']:
        origin = async_origin(source[prebuffer['id']])
        matches = feedback.get(origin, []) if origin is not None else []
        prebuffer['presentation_ids'] = list(matches)
        prebuffer['presentation_link'] = ('unique' if len(matches) == 1 else
            'ambiguous' if matches else 'missing' if origin is not None else 'missing_async_identity')
        for match in matches:
            by_id[match]['prebuffer_ids'].append(prebuffer['id'])
    for record in stages['reported_presentation']:
        parents = record['prebuffer_ids']
        if len(parents) == 1 and len(by_id[parents[0]]['presentation_ids']) == 1:
            parent = by_id[parents[0]]
            if record['pts_status'] == 'missing' and parent['pts_status'] == 'valid':
                record.update(pts_us=parent['pts_us'], pts_status='valid',
                              pts_candidates_us=[parent['pts_us']], pts_source='unique_prebuffer_async_origin')
        record['feedback_success'] = 'not_established_by_named_scope'
    for construction in stages['construction']:
        linked = construction['attempt_ids']
        candidate_feedback = []
        status = 'construction_attempt_link_unresolved'
        if len(linked) == 1:
            prebuffers = by_id[linked[0]]['prebuffer_ids']
            candidate_feedback = sorted({feedback_id for prebuffer_id in prebuffers
                for feedback_id in by_id[prebuffer_id]['presentation_ids']})
            if len(prebuffers) != 1:
                status = 'prebuffer_link_missing' if not prebuffers else 'prebuffer_link_ambiguous'
            elif len(candidate_feedback) != 1:
                status = 'missing' if not candidate_feedback else 'ambiguous'
            elif len(by_id[candidate_feedback[0]]['prebuffer_ids']) != 1:
                status = 'ambiguous'
            else:
                status = ('known_endpoint' if by_id[candidate_feedback[0]]['scope_state'] == 'closed'
                          else 'unclosed_or_invalid_feedback_scope')
        construction['reported_presentation'] = {'status': status, 'ids': candidate_feedback,
                                                'physical_scanout_proven': False}

    selection_threads = collections.defaultdict(set)
    for selection in stages['selection']:
        selection_threads[selection['pid']].add(selection['tid'])
    buckets = collections.defaultdict(list)
    for record in records:
        # Unknown/conflicting PTS occurrences never collapse into one frame.
        key = (record['pid'], record['pts_us'], None if record['pts_status'] == 'valid' else record['id'])
        buckets[key].append(record)
    cohorts = []
    for (pid, pts, separate), rows in buckets.items():
        members = {stage: [row['id'] for row in rows if row['stage'] == stage] for stage in STAGES.values()}
        labels = ['pid_pts_is_candidate_identity' if pts is not None else 'frame_identity_unresolved']
        if any(row['stage'] == 'preparation' and row['scope_state'] == 'closed' for row in rows) and not members['selection']:
            labels.append('prepared_without_selection_candidate_not_confirmed_drop')
        if members['selection'] and not members['attempt']:
            labels.append('selected_without_observed_attempt')
        if members['selection'] and not any(row['stage'] == 'attempt' and row['scope_state'] == 'closed' for row in rows):
            labels.append('selected_without_completed_attempt')
        if members['selection'] and not members['construction']:
            labels.append('selected_without_observed_construction')
        if any(len(members[stage]) > 1 for stage in ('preparation', 'selection')):
            labels.append('repeated_pts_scopes_not_unique_frame_identity')
        if len(selection_threads.get(pid, set())) > 1:
            labels.append('multiple_selection_threads_process_stream_ambiguous')
        if any(row['scope_state'] != 'closed' for row in rows):
            labels.append('unclosed_unlinked_or_invalid_scope')
        if any(row['start_window']['boundary_uncertain'] or row['measurement_boundary_crossing'] for row in rows):
            labels.append('measurement_boundary_sensitive')
        if any(row.get('attempt_link') == 'ambiguous' or 'conflicting_attempt_pts_us' in row for row in rows):
            labels.append('attempt_join_ambiguous_or_conflicting')
        cohorts.append({'pid': pid, 'pts_us': pts, 'unresolved_occurrence_id': separate,
                        'producer_tids': sorted({row['tid'] for row in rows}),
                        'occurrence_ids_by_stage': members, 'labels': labels,
                        'any_start_nominally_in_window': any(row['start_window']['nominal'] == 'inside' for row in rows),
                        'absence_is_limited_to_retained_trace': True,
                        'confirmed_drop_attribution': None})

    drop_producers = collections.defaultdict(list)
    for event in inv['drop_stat_updates']:
        args = events[event['index']].get('args', {})
        count = args.get('count') if isinstance(args, dict) else None
        valid = number(count) and int(count) == count and count >= 0
        drop_producers[(event['pid'], event['tid'])].append({
            'trace_index': event['index'], 'timestamp_us': event['ts'],
            'count': int(count) if valid else None, 'count_valid': valid,
            'source_args': inventory.compact(args), 'window': window_position(event['ts'], alignment)})
    producer_rows = []
    for (pid, tid), rows in sorted(drop_producers.items()):
        producer_rows.append({'pid': pid, 'tid': tid, 'updates': rows,
            'valid_count_sum_whole_trace': sum(row['count'] for row in rows if row['count_valid']),
            'valid_count_sum_nominal_window': sum(row['count'] for row in rows if
                row['count_valid'] and row['window']['nominal'] == 'inside'),
            'invalid_count_updates': sum(not row['count_valid'] for row in rows),
            'attribution': ('outside_selected_processes' if pid not in selection_threads else
                'multiple_selection_threads_same_process' if len(selection_threads[pid]) > 1 else
                'same_process_is_not_stream_identity'),
            'timestamps_are_batched_publication_not_original_drop_times': True})

    pairing = inv['pairing']
    markers = []
    for index, event in enumerate(events):
        if isinstance(event, dict) and re.search(r'overflow|lost.?event|data.?loss|buffer.?wrap', event.get('name', ''), re.I):
            markers.append({'trace_index': index, 'event': inventory.compact(event)})
    stats = inv['trace_metadata_evidence'].get('trace_processor_stats', {})
    return {'schema': SCHEMA, 'alignment': alignment,
        'window_convention': 'Half-open [start,end); uncertainty marked separately. Stage occurrence timestamps differ from counter publication timestamps.',
        'summary': {'stage_occurrences': {stage: len(stages[stage]) for stage in STAGES.values()},
            'stage_occurrences_starting_in_nominal_window': {stage: sum(row['start_window']['nominal'] == 'inside' for row in stages[stage]) for stage in STAGES.values()},
            'candidate_cohort_buckets': len(cohorts),
            'prepared_without_selection_candidate_buckets': sum('prepared_without_selection_candidate_not_confirmed_drop' in row['labels'] for row in cohorts),
            'construction_presentation_status': dict(collections.Counter(row['reported_presentation']['status'] for row in stages['construction'])),
            'drop_counter_producers': len(producer_rows),
            'multiple_drop_producers': len(producer_rows) > 1,
            'drop_producers_outside_selected_processes': sum(row['attribution'] == 'outside_selected_processes' for row in producer_rows),
            'drop_stream_attribution_unresolved': bool(producer_rows),
            'unclosed_unlinked_or_invalid_stage_occurrences': sum(row['scope_state'] != 'closed' for row in records)},
        'occurrences': records, 'candidate_cohorts': cohorts,
        'drop_counter_producers': producer_rows,
        'coverage': {'event_count': inv['trace_event_count'],
            'event_timestamp_bounds_us': inv['event_timestamp_bounds_us'],
            'thread_roles': inv['thread_roles'], 'inventory_coverage': inv['coverage'],
            'trace_processor_stats': stats, 'nonzero_loss_or_failure_stats': nonzero_loss_stats(stats),
            'explicit_loss_markers': markers,
            'pairing': pairing,
            'pairing_previews_truncated': {label: pairing[count] > len(pairing[label]) for label, count in
                (('pending_begins', 'pending_begin_count'), ('unmatched_ends', 'unmatched_end_count'),
                 ('malformed', 'malformed_count'), ('closed_boundary_crossings', 'closed_boundary_crossing_count'))},
            'target_stage_unclosed_records_enumerated_from_all_raw_events': True,
            'complete_producer_coverage_proven': False},
        'limits': [
            'No numerical drop-counter residual is assigned to any stage.',
            'PID/PTS buckets lack stream, reset and actual VideoFrame identity; repeated or cross-thread scopes remain distinct.',
            'Prepared-without-selection is a candidate observed absence, not a confirmed discard or decoder-starvation verdict.',
            'Unconditional AppendQuads indicates frame construction; it precedes actual SubmitCompositorFrame and proves neither ACK receipt nor submission acceptance.',
            'Named presentation endpoints carry no interpreted success/failure state and prove neither physical scanout nor that every submitted frame received feedback.',
            'Prebuffer and named presentation scope starts can be backdated to decode time; their start-window counts are not submission or presentation counts.',
            'Missing stages or endpoints can reflect an untraced path, trace censoring, reset or identity mismatch; absence is not an explicit return reason.',
            'Global bounds and zero reported loss cannot establish every producer was traced. External coverage and fixture receipts remain necessary.',
            'Scope duration includes nested work and scheduling; it is not CPU service or kernel-lock time.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace')
    parser.add_argument('--trace-manifest', action='store_true')
    parser.add_argument('--alignment', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    resource_limits()
    trace, identity = load_trace(args.trace, manifest=args.trace_manifest)
    alignment, alignment_identity = read_json(args.alignment, 1024*1024)
    events = trace_events(trace)
    metadata = {key: value for key, value in trace.items() if key != 'traceEvents'} if isinstance(trace, dict) else {}
    result = analyze(events, metadata, alignment)
    result['input'] = identity
    result['alignment_input'] = alignment_identity
    result['parser_inputs'] = parser_identity(__file__)
    result['parser_inputs'].update(inventory.parser_identity(inventory.__file__))
    output = write_json(args.output, result)
    print(json.dumps({'output': output, 'summary': result['summary']}))


if __name__ == '__main__':
    main()
