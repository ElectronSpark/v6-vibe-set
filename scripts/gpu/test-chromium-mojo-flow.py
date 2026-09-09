#!/usr/bin/env python3
"""Adversarial joins for the pinned Mojo flow export, using synthetic traces."""
import copy
import unittest

import chromium_mojo_flow as flow


def fixture():
    method = next(m for m in flow.METHODS.values()
                  if m['method'] == 'FlushNotifications')
    nodes = [
        {'name': 'mojo::Message::Message', 'pid': 10, 'tid': 11, 'ts': 10,
         'ph': 'X', 'dur': 2, 'cat': 'mojom', 'args': {'flags': 0}},
        {'name': 'Send mojo message', 'pid': 10, 'tid': 11, 'ts': 15,
         'ph': 'I', 'cat': 'mojom'},
        {'name': 'Connector::DispatchMessage', 'pid': 20, 'tid': 21, 'ts': 25,
         'ph': 'X', 'dur': 20, 'cat': 'mojom'},
        {'name': 'Receive mojo message', 'pid': 20, 'tid': 21, 'ts': 30,
         'ph': 'X', 'dur': 10, 'cat': 'mojom', 'args': {
             'chrome_mojo_event_info': {'mojo_interface_tag': method['interface_tag'],
                                        'ipc_hash': method['ipc_hash']}}}]
    events = copy.deepcopy(nodes)
    for index in range(3):
        for phase, node in (('s', nodes[index]), ('f', nodes[index + 1])):
            event = {k: node[k] for k in ('pid', 'tid', 'ts')}
            event.update(ph=phase, cat='mojom.flow', name='message', id=index + 1)
            if phase == 'f':
                event['bp'] = 'e'
            events.append(event)
    return events


def diagnostic_fixture():
    events = fixture()
    batches = []
    for direction, pid, tid, times, instance, batch_id in (
            ('send', 10, 11, (5, 20), 1, 7),
            ('receive', 20, 21, (32, 38), 6, 99)):
        boundary_indices = []
        for which, ts in zip(('enter', 'exit'), times):
            boundary_indices.append(len(events))
            events.append({'name': 'VideoBottleneck', 'cat': 'disabled-by-default-media.bottleneck',
                           'ph': 'I', 'ts': ts, 'pid': pid, 'tid': tid, 'args': {'bt': {
                               'schema_version': 1, 'kind': 'bundle.' + direction + '.' + which,
                               'object': 'bundle', 'instance_id': str(instance),
                               'batch_id': str(batch_id), 'bundle_client_id': '100', 'bundle_id': '50'}}})
        batches.append({'pid': pid, 'object': 'bundle', 'instance_id': instance,
                        'direction': direction, 'local_batch_id': batch_id,
                        'enter_index': boundary_indices[0], 'exit_index': boundary_indices[1],
                        'declared_counts': {'ack': 1, 'reclaim': 0, 'begin_frame': 0},
                        'complete_consistent_batch': True,
                        'members': [{'entry_kind': 'ack', 'entry_index': 0,
                                     'signature': ['ack', 100, 111, 0]}]})
    return events, batches


class FlowTest(unittest.TestCase):
    def status(self, events):
        result = flow.reconstruct(events)
        self.assertEqual(len(result['receives']), 1)
        return result['receives'][0]['status']

    def test_unique_flow_and_elapsed_intervals(self):
        result = flow.reconstruct(fixture())
        self.assertEqual(result['statuses'], {
            'unique_constructor_send_connector_receive': 1})
        row = result['receives'][0]
        self.assertEqual([n['event_index'] for n in row['reverse_chain']], [3, 2, 1, 0])
        self.assertEqual(row['intervals_us']['constructor_to_receive'], 20)
        self.assertEqual(row['intervals_us']['send_to_connector'], 10)

    def test_missing_edge(self):
        self.assertEqual(self.status(fixture()[:-2]),
                         'missing_or_ambiguous_incoming_edge')

    def test_duplicate_finish_poisons_coordinate(self):
        events = fixture()
        events.append(copy.deepcopy(events[-1]))
        self.assertEqual(self.status(events), 'coordinate_touched_by_invalid_evidence')

    def test_broken_alternative_must_not_make_valid_edge_unique(self):
        events = fixture()
        events.append(dict(events[-1], id=400))
        self.assertEqual(self.status(events), 'coordinate_touched_by_invalid_evidence')

    def test_two_valid_incoming_edges(self):
        events = fixture()
        events.extend(dict(e, id=400) for e in events[-2:])
        self.assertEqual(self.status(events), 'missing_or_ambiguous_incoming_edge')

    def test_unrelated_same_coordinate_is_ambiguous(self):
        events = fixture()
        events.append(dict(events[1], name='VideoBottleneck'))
        self.assertEqual(self.status(events), 'ambiguous_or_unexpected_node')

    def test_string_id_and_id2_are_not_guessed(self):
        for edit in ({'id': '0x1'}, {'id2': {'global': '1'}}):
            with self.subTest(edit=edit):
                events = fixture()
                events[4].update(edit)
                self.assertEqual(self.status(events), 'unsupported_flow_export')

    def test_bad_binding_and_backwards_edge(self):
        for edit in ({'bp': 's'}, {'ts': 1}, {'cat': 'other'}):
            with self.subTest(edit=edit):
                events = fixture()
                events[-1].update(edit)
                result = flow.reconstruct(events)
                self.assertEqual(result['invalid_pair_count'], 1)
                self.assertNotIn('intervals_us', result['receives'][0])

    def test_response_and_bool_flags_are_rejected(self):
        for flags in (True, 1, 2, None, '0'):
            with self.subTest(flags=flags):
                events = fixture()
                events[0]['args']['flags'] = flags
                self.assertEqual(self.status(events), 'constructor_not_one_way_request')
        events = fixture()
        events[3]['name'] = 'Receive mojo reply'
        self.assertEqual(flow.reconstruct(events)['receives'], [])

    def test_unfinished_and_bad_duration_scopes(self):
        for edit in ({'ph': 'B'}, {'dur': -1}, {'dur': float('nan')}):
            with self.subTest(edit=edit):
                events = fixture()
                events[3].update(edit)
                self.assertEqual(self.status(events), 'invalid_receive_scope')
        events = fixture()
        events[2]['ph'] = 'B'
        self.assertEqual(self.status(events), 'incomplete_chain_scope')

    def test_malformed_fields_do_not_crash_or_join(self):
        events = fixture()
        events.extend([None, {'ph': []}, {'ph': 'X', 'args': []}])
        result = flow.reconstruct(events)
        self.assertEqual(result['malformed_event_count'], 3)
        events = fixture()
        events[3]['args']['chrome_mojo_event_info']['mojo_interface_tag'] = []
        self.assertEqual(flow.reconstruct(events)['receives'], [])

    def test_bad_coordinate_cannot_invent_cross_process_timing(self):
        events = fixture()
        events[-1]['pid'] = True
        result = flow.reconstruct(events)
        self.assertEqual(result['invalid_pair_count'], 1)
        self.assertNotIn('intervals_us', result['receives'][0])

    def test_completed_constructor_cannot_extend_beyond_send(self):
        events = fixture()
        events[0]['dur'] = 100
        self.assertEqual(self.status(events), 'inconsistent_completed_chain_scopes')

    def test_receive_cannot_extend_beyond_connector_dispatch(self):
        events = fixture()
        events[3]['dur'] = 100
        self.assertEqual(self.status(events), 'inconsistent_completed_chain_scopes')

    def test_send_must_have_pinned_instant_phase(self):
        events = fixture()
        events[1].update(ph='X', dur=100)
        self.assertEqual(self.status(events), 'incomplete_chain_scope')

    def test_scope_end_must_be_finite(self):
        events = fixture()
        events[0].update(ts=1e308, dur=1e308)
        events[4]['ts'] = 1e308
        self.assertNotIn('intervals_us', flow.reconstruct(events)['receives'][0])


class BatchJoinTest(unittest.TestCase):
    def test_different_local_ids_join_only_by_real_flow(self):
        events, batches = diagnostic_fixture()
        result = flow.link_batches(events, batches)
        self.assertEqual(result['accepted_count'], 1)
        link = result['links'][0]
        self.assertEqual(link['sender_enter_index'], batches[0]['enter_index'])
        self.assertEqual(link['receiver_enter_index'], batches[1]['enter_index'])
        self.assertEqual(len(link['evidence']['edges']), 3)

    def test_equal_ids_and_membership_without_flow_do_not_join(self):
        events, batches = diagnostic_fixture()
        batches[1]['local_batch_id'] = 7
        for which in ('enter', 'exit'):
            events[batches[1][which + '_index']]['args']['bt']['batch_id'] = '7'
        events[8]['ph'] = 'I'
        result = flow.link_batches(events, batches)
        self.assertEqual(result['accepted_count'], 0)

    def test_wrong_sink_or_resource_membership_rejected(self):
        for field, value in ((1, 999), (2, 222), (3, 1)):
            with self.subTest(field=field):
                events, batches = diagnostic_fixture()
                batches[1]['members'][0]['signature'][field] = value
                result = flow.link_batches(events, batches)
                self.assertEqual(result['links'][0]['status'], 'ordered_batch_membership_mismatch')

    def test_wrong_bundle_identity_rejected(self):
        events, batches = diagnostic_fixture()
        events[batches[1]['enter_index']]['args']['bt']['bundle_id'] = '60'
        result = flow.link_batches(events, batches)
        self.assertEqual(result['links'][0]['status'], 'bundle_identity_mismatch')

    def test_boundary_equalities_remain_ambiguous(self):
        for side, which, ts in ((0, 'enter', 10), (0, 'exit', 15),
                                (1, 'enter', 30), (1, 'exit', 40)):
            with self.subTest(side=side, which=which):
                events, batches = diagnostic_fixture()
                events[batches[side][which + '_index']]['ts'] = ts
                self.assertEqual(flow.link_batches(events, batches)['accepted_count'], 0)

    def test_nearest_sender_is_not_enclosing_sender(self):
        events, batches = diagnostic_fixture()
        events[batches[0]['enter_index']]['ts'] = 11
        self.assertEqual(flow.link_batches(events, batches)['accepted_count'], 0)

    def test_overlapping_sender_even_if_incomplete_blocks_unique_join(self):
        events, batches = diagnostic_fixture()
        extra = copy.deepcopy(batches[0])
        extra['complete_consistent_batch'] = False
        batches.append(extra)
        self.assertEqual(flow.link_batches(events, batches)['accepted_count'], 0)

    def test_incomplete_batch_not_promoted_by_good_flow(self):
        for side in (0, 1):
            with self.subTest(side=side):
                events, batches = diagnostic_fixture()
                batches[side]['complete_consistent_batch'] = False
                result = flow.link_batches(events, batches)
                self.assertEqual(result['links'][0]['status'], 'incomplete_or_inconsistent_local_batch')

    def test_zero_ack_batch_has_transport_but_no_ack_members(self):
        events, batches = diagnostic_fixture()
        for batch in batches:
            batch['declared_counts']['ack'] = 0
            batch['members'] = []
        result = flow.link_batches(events, batches)
        self.assertEqual(result['accepted_count'], 1)
        self.assertEqual(batches[1]['members'], [])

    def test_reordered_members_rejected(self):
        events, batches = diagnostic_fixture()
        for batch in batches:
            batch['members'].append({'entry_kind': 'ack', 'entry_index': 1,
                                     'signature': ['ack', 100, 222, 0]})
            batch['declared_counts']['ack'] = 2
        batches[1]['members'][0]['entry_index'] = 1
        batches[1]['members'][1]['entry_index'] = 0
        self.assertEqual(flow.link_batches(events, batches)['links'][0]['status'],
                         'ordered_batch_membership_mismatch')

    def test_nested_generic_receive_does_not_hide_ambiguous_owner(self):
        events, batches = diagnostic_fixture()
        events.append(dict(events[3], ts=31, dur=8))
        result = flow.link_batches(events, batches)
        self.assertEqual(result['accepted_count'], 0)
        self.assertEqual(result['links'][0]['status'], 'ambiguous_batch_boundary')

    def test_one_sender_cannot_be_used_for_two_deliveries(self):
        events, batches = diagnostic_fixture()
        second_events, second_batches = diagnostic_fixture()
        offset = len(events)
        for event in second_events:
            event['ts'] += 30
            if event.get('ph') in ('s', 'f'):
                event['id'] += 10
        for batch in second_batches:
            batch['enter_index'] += offset
            batch['exit_index'] += offset
        events.extend(second_events)
        # Only the first sender is a locally validated boundary, encompassing
        # both messages. Two different receive batches cannot each claim it.
        events[batches[0]['exit_index']]['ts'] = 49
        batches.append(second_batches[1])
        result = flow.link_batches(events, batches)
        self.assertEqual(result['accepted_count'], 0)
        self.assertEqual([r['status'] for r in result['links']],
                         ['diagnostic_batch_used_by_multiple_flows'] * 2)

    def test_missing_second_receiver_cannot_make_first_sender_unique(self):
        events, batches = diagnostic_fixture()
        extra = fixture()
        for event in extra:
            event['ts'] += 30
            if event.get('ph') in ('s', 'f'):
                event['id'] += 10
        events.extend(extra)
        events[batches[0]['exit_index']]['ts'] = 49
        result = flow.link_batches(events, batches)
        self.assertEqual(result['accepted_count'], 0)
        self.assertEqual(result['links'][0]['status'],
                         'diagnostic_batch_used_by_multiple_flows')

    def test_inconsistent_second_receiver_cannot_make_first_sender_unique(self):
        events, batches = diagnostic_fixture()
        extra, extra_batches = diagnostic_fixture()
        offset = len(events)
        for event in extra:
            event['ts'] += 30
            if event.get('ph') in ('s', 'f'):
                event['id'] += 10
        events.extend(extra)
        target = extra_batches[1]
        target['enter_index'] += offset
        target['exit_index'] += offset
        target['complete_consistent_batch'] = False
        batches.append(target)
        events[batches[0]['exit_index']]['ts'] = 49
        result = flow.link_batches(events, batches)
        self.assertEqual(result['accepted_count'], 0)
        self.assertEqual(result['links'][0]['status'],
                         'diagnostic_batch_used_by_multiple_flows')

    def test_second_send_without_downstream_flow_still_blocks_uniqueness(self):
        events, batches = diagnostic_fixture()
        events.append(dict(events[1], ts=16))
        result = flow.link_batches(events, batches)
        self.assertEqual(result['accepted_count'], 0)
        self.assertEqual(result['links'][0]['status'], 'ambiguous_source_send_observations')


if __name__ == '__main__':
    unittest.main()
