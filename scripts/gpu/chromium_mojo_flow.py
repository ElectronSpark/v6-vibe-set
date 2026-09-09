#!/usr/bin/env python3
"""Strict reconstruction of Chromium's exported one-way Mojo ACK flows.

Exported integer s/f IDs identify edges, not original wire nonces. This module
never matches messages by time proximity, byte count, or a diagnostic batch ID.
It recognizes only the export shape observed in the pinned Chromium capture.
"""
import argparse
import bisect
import collections
import hashlib

from chromium_trace_common import (load_trace, number, parser_identity,
                                   resource_limits, trace_events, write_json)


METHODS = {}
for interface, method in (
        ('FrameSinkBundleClient', 'FlushNotifications'),
        ('CompositorFrameSinkClient', 'DidReceiveCompositorFrameAck')):
    full_name = 'viz::mojom::' + interface + '::' + method
    digest = int(hashlib.sha256(full_name.encode()).hexdigest()[:8], 16)
    METHODS[('viz.mojom.' + interface, digest)] = {
        'interface_tag': 'viz.mojom.' + interface, 'ipc_hash': digest,
        'name': full_name, 'method': method}


def coordinate(event):
    if (not number(event.get('ts')) or event['ts'] < 0
            or any(type(event.get(k)) is not int or event[k] < 0
                   for k in ('pid', 'tid'))):
        return None
    return event['pid'], event['tid'], event['ts']


def identify_receive(event):
    if not {'mojom', 'toplevel'}.intersection(event.get('cat', '').split(',')):
        return None
    info = event.get('args', {}).get('chrome_mojo_event_info')
    if not isinstance(info, dict):
        return None
    raw = info.get('ipc_hash')
    if isinstance(raw, str):
        try:
            raw = int(raw, 0)
        except ValueError:
            return None
    if type(raw) is not int or not isinstance(info.get('mojo_interface_tag'), str):
        return None
    method = METHODS.get((info.get('mojo_interface_tag'), raw))
    if method and event.get('name') in {
            'Receive mojo message', 'Receive ' + method['name'],
            'Receive ' + method['name'].replace('viz::mojom::',
                                               'viz::mojom::blink::')}:
        return method
    return None


def short(index, event):
    return {'event_index': index, 'name': event.get('name'),
            'phase': event.get('ph'),
            'pid': event.get('pid'), 'tid': event.get('tid'),
            'ts_us': event.get('ts'), 'duration_us': event.get('dur')}


def reconstruct(events):
    """Return independently proven chains and explicit reasons for exclusions.

    Chains describe elapsed time in a single exported trace clock. They do not
    identify ACK-array members or establish scheduler/CPU/physical-display time.
    The caller must validate loss coverage and diagnostic local batch histories.
    """
    trace_events(events)
    nodes = collections.defaultdict(list)
    pairs = collections.defaultdict(list)
    poisoned = set()
    receives = []
    malformed = []
    malformed_count = 0
    unsupported_flow_count = 0

    def reject(index, reason):
        nonlocal malformed_count
        malformed_count += 1
        if len(malformed) < 100:
            malformed.append({'event_index': index, 'reason': reason})

    for index, event in enumerate(events):
        if not isinstance(event, dict):
            reject(index, 'non-object event')
            continue
        coord = coordinate(event)
        phase = event.get('ph')
        if (not isinstance(phase, str)
                or not isinstance(event.get('name', ''), str)
                or not isinstance(event.get('cat', ''), str)
                or not isinstance(event.get('args', {}), dict)):
            reject(index, 'invalid event fields')
            if coord is not None:
                poisoned.add(coord)
            # An unparseable flow can alias another edge ID.
            if phase in ('s', 't', 'f'):
                unsupported_flow_count += 1
            continue
        if phase in ('s', 't', 'f'):
            flow_id = event.get('id')
            if (type(flow_id) is not int or flow_id < 0 or 'id2' in event
                    or event.get('scope') is not None):
                unsupported_flow_count += 1
                reject(index, 'unsupported flow identity or scope')
                if coord is not None:
                    poisoned.add(coord)
                continue
            pairs[flow_id].append((index, event))
        elif phase != 'M' and coord is not None:
            # Keep even unrelated nodes: equal exported coordinates do not
            # establish which event a flow attaches to.
            nodes[coord].append((index, event))
        if identify_receive(event):
            receives.append((index, event, identify_receive(event)))

    incoming = collections.defaultdict(list)
    invalid_pair_count = 0
    invalid_pairs = []
    for flow_id, pair in pairs.items():
        starts = [p for p in pair if p[1]['ph'] == 's']
        finishes = [p for p in pair if p[1]['ph'] == 'f']
        reason = None
        if len(pair) != 2 or len(starts) != 1 or len(finishes) != 1:
            reason = 'nonunique or incomplete flow pair'
        else:
            (si, start), (fi, finish) = starts[0], finishes[0]
            if coordinate(start) is None or coordinate(finish) is None:
                reason = 'invalid endpoint coordinate'
            elif ((start.get('name'), start.get('cat')) !=
                  (finish.get('name'), finish.get('cat'))
                  or finish.get('bp') != 'e'):
                reason = 'inconsistent endpoint name/category/binding'
            elif finish['ts'] < start['ts']:
                reason = 'backwards flow'
        if reason:
            invalid_pair_count += 1
            if len(invalid_pairs) < 100:
                invalid_pairs.append({'flow_id': flow_id, 'reason': reason})
            # Ignoring a broken edge must not make another edge appear unique.
            poisoned.update(c for _, e in pair if (c := coordinate(e)) is not None)
            continue
        incoming[coordinate(finish)].append({
            'flow_id': flow_id, 'source_index': si, 'end_index': fi,
            'source_coordinate': coordinate(start)})

    rows = []
    for index, receive, method in receives:
        row = {'receive': short(index, receive), 'method': method,
               'status': 'unresolved', 'reverse_chain': [], 'edges': []}
        rows.append(row)
        if (coordinate(receive) is None or receive.get('ph') != 'X'
                or not number(receive.get('dur')) or receive['dur'] < 0):
            row['status'] = 'invalid_receive_scope'
            continue
        if unsupported_flow_count:
            row['status'] = 'unsupported_flow_export'
            continue
        node = coordinate(receive)
        visited = set()
        for depth, expected in enumerate((receive['name'],
                                          'Connector::DispatchMessage',
                                          'Send mojo message',
                                          'mojo::Message::Message')):
            if node in poisoned:
                row['status'] = 'coordinate_touched_by_invalid_evidence'
                break
            candidates = nodes.get(node, [])
            if len(candidates) != 1 or candidates[0][1].get('name') != expected:
                row['status'] = 'ambiguous_or_unexpected_node'
                row['node_candidate_count'] = len(candidates)
                row['node_candidate_indices'] = [i for i, _ in candidates[:100]]
                break
            ci, candidate = candidates[0]
            expected_phase = 'I' if depth == 2 else 'X'
            if (candidate['ph'] != expected_phase
                    or (candidate['ph'] == 'X' and
                        (not number(candidate.get('dur')) or candidate['dur'] < 0
                         or not number(candidate['ts'] + candidate['dur'])))
                    or (candidate['ph'] == 'I' and 'dur' in candidate)):
                row['status'] = 'incomplete_chain_scope'
                break
            row['reverse_chain'].append(short(ci, candidate))
            if depth == 3:
                flags = candidate.get('args', {}).get('flags')
                if incoming.get(node):
                    row['status'] = 'constructor_has_prior_flow'
                elif type(flags) is not int or flags != 0:
                    row['status'] = 'constructor_not_one_way_request'
                else:
                    row['status'] = 'unique_constructor_send_connector_receive'
                break
            edges = incoming.get(node, [])
            if len(edges) != 1:
                row['status'] = 'missing_or_ambiguous_incoming_edge'
                row['incoming_edge_count'] = len(edges)
                break
            edge = edges[0]
            if edge['flow_id'] in visited:
                row['status'] = 'flow_cycle'
                break
            visited.add(edge['flow_id'])
            row['edges'].append({k: edge[k] for k in
                                 ('flow_id', 'source_index', 'end_index')})
            node = edge['source_coordinate']
        if row['status'] == 'unique_constructor_send_connector_receive':
            recv, connector, send, constructor = row['reverse_chain']
            if ((constructor['pid'], constructor['tid']) != (send['pid'], send['tid'])
                    or (connector['pid'], connector['tid']) != (recv['pid'], recv['tid'])
                    or constructor['ts_us'] + constructor['duration_us'] > send['ts_us']
                    or recv['ts_us'] + recv['duration_us'] > connector['ts_us'] + connector['duration_us']):
                row['status'] = 'inconsistent_completed_chain_scopes'
                continue
            row['intervals_us'] = {
                'constructor_to_send': send['ts_us'] - constructor['ts_us'],
                'send_to_connector': connector['ts_us'] - send['ts_us'],
                'connector_to_receive': recv['ts_us'] - connector['ts_us'],
                'constructor_to_receive': recv['ts_us'] - constructor['ts_us'],
                'receive_dispatch_scope': receive['dur']}
    return {'schema_version': 1, 'flow_pair_count': len(pairs),
            'invalid_pair_count': invalid_pair_count,
            'invalid_pair_examples': invalid_pairs,
            'malformed_event_count': malformed_count,
            'malformed_event_examples': malformed,
            'unsupported_flow_count': unsupported_flow_count,
            'statuses': dict(collections.Counter(r['status'] for r in rows)),
            'receives': rows,
            'limits': [
                'Integer export IDs identify edges, not wire nonces or local diagnostic batch IDs.',
                'Only unique completed constructor/send/Connector/receive paths in the pinned X/I/X/X export shape are admitted; timestamp proximity is never a join.',
                'Malformed or unsupported flow identities suppress joins rather than guessing their scope.',
                'Elapsed intervals include serialization, dispatch, transport and scheduling; they do not isolate CPU time.',
                'Cross-process elapsed intervals require the exporter shared trace clock; independently merged clocks need separate validation.',
                'Diagnostic batch membership, lifecycle continuity and trace-loss coverage require separate validation.',
            ]}


class IntervalIndex:
    """Bounded containment queries, including weak/equal-bound candidates."""
    def __init__(self, intervals):
        self.rows = sorted(intervals, key=lambda row: row[0])
        self.starts = [row[0] for row in self.rows]
        self.max_ends = []
        end = -1
        for row in self.rows:
            end = max(end, row[1])
            self.max_ends.append(end)

    def enclosing(self, start, end, budget):
        matches = []
        index = bisect.bisect_right(self.starts, start) - 1
        while index >= 0 and self.max_ends[index] >= end:
            budget[0] -= 1
            if budget[0] < 0:
                raise ValueError('batch containment comparison budget exceeded')
            a, b, value = self.rows[index]
            if b >= end:
                matches.append((a, b, value))
            index -= 1
        return matches


def link_batches(events, batches):
    """Join locally validated diagnostic batches through actual Mojo edges.

    `batches` comes from chromium-submission-diagnostics.analyze_batches. Local
    IDs are used only to validate a batch's own envelope, never across processes.
    Weak containment remains a candidate so an ambiguous sibling cannot vanish
    merely because another candidate passed stricter validation.
    """
    transport = reconstruct(events)
    chains = transport['receives']
    receive_intervals = collections.defaultdict(list)
    for row_index, row in enumerate(chains):
        event = events[row['receive']['event_index']]
        if (row['method']['method'] == 'FlushNotifications'
                and coordinate(event) is not None and event.get('ph') == 'X'
                and number(event.get('dur')) and event['dur'] > 0):
            receive_intervals[(event['pid'], event['tid'])].append(
                (event['ts'], event['ts'] + event['dur'], row_index))
    receivers = {key: IntervalIndex(rows) for key, rows in receive_intervals.items()}
    source_intervals = collections.defaultdict(list)
    received_batches = collections.defaultdict(list)
    envelopes = {}
    issues = []
    budget = [1_500_000]

    def endpoint(batch, which):
        index = batch.get(which + '_index')
        if type(index) is not int or not 0 <= index < len(events):
            return None
        event = events[index]
        if not isinstance(event, dict) or coordinate(event) is None:
            return None
        args = event.get('args')
        bt = args.get('bt') if isinstance(args, dict) else None
        expected = 'bundle.' + batch.get('direction', '?') + '.' + which
        if (not isinstance(bt, dict) or bt.get('schema_version') != 1
                or bt.get('kind') != expected
                or event.get('name') != 'VideoBottleneck'
                or bt.get('object') != batch.get('object')
                or bt.get('instance_id') != str(batch.get('instance_id'))
                or bt.get('batch_id') != str(batch.get('local_batch_id'))
                or str(event['pid']) != str(batch.get('pid'))):
            return None
        return event

    for batch_index, batch in enumerate(batches):
        enter, leave = endpoint(batch, 'enter'), endpoint(batch, 'exit')
        if (enter is None or leave is None or
                coordinate(enter)[:2] != coordinate(leave)[:2] or
                leave['ts'] < enter['ts']):
            issues.append({'batch_index': batch_index, 'reason': 'invalid_batch_envelope'})
            continue
        key = coordinate(enter)[:2]
        envelopes[batch_index] = enter, leave
        if batch['direction'] == 'send':
            source_intervals[key].append((enter['ts'], leave['ts'], batch_index))
        elif batch['direction'] == 'receive':
            index = receivers.get(key)
            candidates = index.enclosing(enter['ts'], leave['ts'], budget) if index else []
            # Store every candidate, even when nesting is ambiguous, so no
            # other batch can falsely obtain unique ownership of this receive.
            for a, b, row_index in candidates:
                received_batches[row_index].append((batch_index,
                    len(candidates) == 1 and a < enter['ts'] <= leave['ts'] < b))
            if len(candidates) != 1:
                issues.append({'batch_index': batch_index,
                               'reason': 'missing_or_ambiguous_enclosing_receive',
                               'candidate_count': len(candidates)})
    senders = {key: IntervalIndex(rows) for key, rows in source_intervals.items()}
    # A lost downstream edge must not erase a second send inside the same
    # diagnostic call. Count source send observations independently of whether
    # they have a complete target receive chain.
    send_observations = collections.defaultdict(list)
    for event_index, event in enumerate(events):
        if (isinstance(event, dict) and event.get('name') == 'Send mojo message'
                and coordinate(event) is not None):
            send_observations[coordinate(event)[:2]].append((event['ts'], event_index))
    for observations in send_observations.values():
        observations.sort()
    links = []
    endpoint_claims = collections.Counter()
    for row_index, row in enumerate(chains):
        if row['method']['method'] != 'FlushNotifications':
            continue
        result = {'receive_event_index': row['receive']['event_index'],
                  'accepted': False, 'status': row['status']}
        links.append(result)
        if row['status'] != 'unique_constructor_send_connector_receive':
            continue
        recv, connector, send, constructor = row['reverse_chain']
        if (send['pid'], send['tid']) != (constructor['pid'], constructor['tid']):
            result['status'] = 'constructor_and_send_on_different_threads'
            continue
        if (recv['pid'], recv['tid']) != (connector['pid'], connector['tid']):
            result['status'] = 'connector_and_receive_on_different_threads'
            continue
        index = senders.get((send['pid'], send['tid']))
        candidates = (index.enclosing(constructor['ts_us'], send['ts_us'], budget)
                      if index else [])
        targets = received_batches.get(row_index, [])
        # Failed alternatives still claim any plausible endpoint. In particular,
        # a second proven send inside one envelope must poison uniqueness even
        # if its diagnostic receive is missing or its membership is damaged.
        for _, _, batch_index in candidates:
            endpoint_claims[('sender', batches[batch_index]['enter_index'])] += 1
        for batch_index, _ in targets:
            endpoint_claims[('receiver', batches[batch_index]['enter_index'])] += 1
        result.update(sender_candidate_count=len(candidates), receiver_candidate_count=len(targets))
        if len(candidates) != 1 or len(targets) != 1:
            result['status'] = 'missing_or_ambiguous_diagnostic_batch'
            continue
        a, b, source_index = candidates[0]
        target_index, strictly_nested = targets[0]
        source, target = batches[source_index], batches[target_index]
        result.update(sender_enter_index=source['enter_index'], sender_exit_index=source['exit_index'],
                      receiver_enter_index=target['enter_index'], receiver_exit_index=target['exit_index'])
        observations = send_observations.get((send['pid'], send['tid']), [])
        first = bisect.bisect_left(observations, (a, -1))
        last = bisect.bisect_right(observations, (b, len(events)))
        result['sender_scope_send_observation_count'] = last - first
        if not a < constructor['ts_us'] <= send['ts_us'] < b or not strictly_nested:
            result['status'] = 'ambiguous_batch_boundary'
            continue
        if not source.get('complete_consistent_batch') or not target.get('complete_consistent_batch'):
            result['status'] = 'incomplete_or_inconsistent_local_batch'
            continue
        source_bt, target_bt = (envelopes[i][0]['args']['bt'] for i in (source_index, target_index))
        bundle_keys = ('bundle_client_id', 'bundle_id')
        if (any(not isinstance(source_bt.get(k), str) or not source_bt[k].isdecimal()
                for k in bundle_keys) or
                any(source_bt.get(k) != target_bt.get(k) for k in bundle_keys)):
            result['status'] = 'bundle_identity_mismatch'
            continue
        def signature(batch):
            return sorted((m['entry_kind'], m['entry_index'], m['signature'])
                          for m in batch['members'])
        if (source.get('declared_counts') != target.get('declared_counts')
                or signature(source) != signature(target)):
            result['status'] = 'ordered_batch_membership_mismatch'
            continue
        result.update(accepted=True, unique=True, status='validated_mojo_batch_delivery',
                      evidence={'nodes': row['reverse_chain'], 'edges': row['edges']},
                      intervals_us=row['intervals_us'],
                      local_batch_ids_were_not_used_as_wire_identity=True)
    # A unique enclosing scope alone is insufficient if another chain claims
    # either endpoint. Reject every colliding candidate, not just the later one.
    for row in links:
        if row.get('accepted') and any(endpoint_claims[(kind, row[kind + '_enter_index'])] != 1
                                       for kind in ('sender', 'receiver')):
            row.update(accepted=False, unique=False, status='diagnostic_batch_used_by_multiple_flows')
        elif row.get('accepted') and row['sender_scope_send_observation_count'] != 1:
            row.update(accepted=False, unique=False, status='ambiguous_source_send_observations')
    return {'schema': 'chromium-bottleneck-flow-links-v1',
            'method': 'unique_exported_mojo_edges_and_strict_diagnostic_batch_envelopes',
            'links': links, 'batch_envelope_issues': issues, 'transport': transport,
            'accepted_count': sum(row['accepted'] for row in links)}


def main():
    resource_limits()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace-manifest', action='store_true')
    parser.add_argument('trace')
    parser.add_argument('output')
    args = parser.parse_args()
    trace, identity = load_trace(args.trace, manifest=args.trace_manifest)
    result = reconstruct(trace_events(trace))
    result.update(input=identity, parser_identity=parser_identity(__file__))
    write_json(args.output, result, 32 * 1024 * 1024)


if __name__ == '__main__':
    main()
