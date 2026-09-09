#!/usr/bin/env python3
"""Synthetic regressions for observed guards, counter history and ACK identity."""
import copy
import importlib.util
import pathlib
import unittest

from chromium_trace_common import resource_limits

spec = importlib.util.spec_from_file_location(
    'diagnostics', pathlib.Path(__file__).with_name('chromium-submission-diagnostics.py'))
diagnostics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostics)

METADATA = {'metadata': {'clock-domain': 'LINUX_CLOCK_MONOTONIC', 'trace_processor_stats': {}}}
ALIGNMENT = dict(validated=True, method='synthetic clock proof', evidence={'test': True},
                 trace_start_us=10, trace_end_us=100, uncertainty_us=0)
SNAPSHOT = dict(reset_epoch=0, binding_epoch=0, pending_ack=0, sink_valid=True, sink_client_id=5, sink_id=6,
    sink_bound=True, bundle_valid=True, bundle_alive=True, bundle_client_id=5, bundle_id=7,
    surface_visible=True, page_visible=True, force_submit=False, force_begin_frames=True,
    is_rendering=True, last_frame_id_valid=False, last_frame_id=0, current_width=1280, current_height=800)
ATTEMPT = dict(attempt_id=1, entry_epoch=0, origin='OnBeginFrame', unique_frame_id=101,
    pts_us=50000, natural_width=1280, natural_height=800, previous_width=1280, previous_height=800,
    guard_stage='entry', output_computed=False, size_change_evaluated=False)
OUTPUT = dict(output_computed=True, output_width=1280, output_height=800,
    size_change_evaluated=True, size_changed=False, guard_stage='accepted')


def event(kind, seq, ts=None, pid=1, tid=2, obj='submitter', instance=1, **fields):
    bt = dict(SNAPSHOT) if obj == 'submitter' else {}
    if kind.startswith('submit.'):
        bt.update(ATTEMPT)
        bt['should_submit_evaluated'] = kind != 'submit.entry' and fields.get('reason') != 'no_sink'
    bt.update(fields)
    bt = {key: str(value) if type(value) is int else value for key, value in bt.items()}
    bt.update(schema_version=1, kind=kind, object=obj, instance_id=str(instance), event_seq=str(seq))
    return {'name': diagnostics.EVENT, 'cat': diagnostics.CATEGORY, 'ph': 'I',
            'pid': pid, 'tid': tid, 'ts': seq*10 if ts is None else ts, 'args': {'bt': bt}}


def accepted():
    return [event('instance.begin', 0), event('submit.entry', 1),
        event('submit.construct', 2, phase='enter', **OUTPUT),
        event('submit.construct', 3, phase='exit', **OUTPUT),
        event('submit.send', 4, phase='enter', **OUTPUT),
        event('submit.send', 5, phase='exit', **OUTPUT),
        event('counter.change', 6, cause='submit', attempt_id=1, pending_before=0, pending_after=1, pending_ack=1),
        event('submit.return', 7, pending_ack=1, reason='accepted', **OUTPUT)]


def callback(start=8, pending=1, callback_id=1, **fields):
    rows = [event('ack.enter', start, pending_ack=pending, callback_id=callback_id, resource_count=0, **fields),
            event('ack.reclaim_done', start+1, pending_ack=pending, callback_id=callback_id, **fields)]
    if pending:
        rows.append(event('counter.change', start+2, pending_ack=pending-1, cause='ack',
                          pending_before=pending, pending_after=pending-1, callback_id=callback_id, **fields))
    rows.append(event('ack.exit', start+len(rows), pending_ack=max(0,pending-1),
                      callback_id=callback_id, applied=bool(pending), **fields))
    return rows


def analyze(rows, **kwargs):
    return diagnostics.analyze(rows, METADATA, **kwargs)


def send_batch(batch=3, members=('ack',), seq=1, ts=10, instance=1):
    counts = {kind: members.count(kind) for kind in ('ack', 'reclaim', 'begin_frame')}
    fields = dict(batch_id=batch, bundle_client_id=5, bundle_id=7,
                  ack_count=counts['ack'], reclaim_count=counts['reclaim'], begin_frame_count=counts['begin_frame'])
    rows = [event('bundle.send.enter', seq, ts=ts, obj='service_group', instance=instance, **fields)]
    indices = dict.fromkeys(counts, 0)
    for offset, kind in enumerate(members):
        rows.append(event('bundle.send.entry', seq+offset+1, ts=ts+offset+1, obj='service_group',
            instance=instance, batch_id=batch, entry_kind=kind, entry_index=indices[kind],
            sink_client_id=5, sink_id=6, resource_count=0))
        indices[kind] += 1
    rows.append(event('bundle.send.exit', seq+len(rows), ts=ts+len(rows), obj='service_group', instance=instance, batch_id=batch))
    return rows


def receive_batch(batch=91, members=('ack',), seq=1, ts=50, found=False):
    counts = {kind: members.count(kind) for kind in ('ack', 'reclaim', 'begin_frame')}
    fields = dict(batch_id=batch, bundle_client_id=5, bundle_id=7,
                  ack_count=counts['ack'], reclaim_count=counts['reclaim'], begin_frame_count=counts['begin_frame'])
    rows = [event('bundle.receive.enter', seq, ts=ts, obj='client_bundle', **fields)]
    indices = dict.fromkeys(counts, 0)
    for kind in members:
        member = dict(batch_id=batch, entry_kind=kind, entry_index=indices[kind], sink_client_id=5, sink_id=6,
                      resource_count=0, client_found=found)
        rows.append(event('bundle.receive.entry', seq+len(rows), ts=ts+len(rows)*10, obj='client_bundle', **member))
        if found:
            rows.append(event('bundle.receive.entry_done', seq+len(rows), ts=ts+len(rows)*10, obj='client_bundle', **member))
        indices[kind] += 1
    rows.append(event('bundle.receive.exit', seq+len(rows), ts=ts+len(rows)*10, obj='client_bundle', batch_id=batch))
    return rows


SURFACE = dict(callback_id=1, client_address=123, client_present=True, called=True,
               frame_token=80, surface_frame_trace_id=900, frame_index=7)
SERVICE = dict(sink_client_id=5, sink_id=6, surface_client_address=123)


def source_chain():
    return [event('instance.begin', 0, ts=5, obj='service_group'),
        event('surface.ack.ready', 1, ts=10, obj='surface_ack', instance=8, **SURFACE),
        event('service.ack.ready', 1, ts=20, obj='service_sink', service_pending=1, **SERVICE),
        event('service.ack.emit', 2, ts=30, obj='service_sink', callback_id=1,
              service_pending=0, pending_before=1, pending_after=0, route='ack', resource_count=0, **SERVICE),
        event('bundle.enqueue', 1, ts=40, obj='service_group', entry_kind='ack', queue_index=0,
              sink_client_id=5, sink_id=6, resource_count=0),
        event('service.ack.done', 3, ts=50, obj='service_sink', callback_id=1, service_pending=0, **SERVICE),
        event('surface.ack.done', 2, ts=60, obj='surface_ack', instance=8, **SURFACE)]


class SourceCallTests(unittest.TestCase):
    def test_surface_trace_id_signed64_and_unset_are_preserved(self):
        for value in (-1, -2, -(2**63), 2**63-1):
            with self.subTest(value=value):
                rows = source_chain()
                for row in rows:
                    if row['args']['bt']['object'] == 'surface_ack':
                        row['args']['bt']['surface_frame_trace_id'] = str(value)
                rows.append(event('service.admit', 0, ts=6, obj='service_sink', instance=3,
                    service_pending=1, pending_before=0, pending_after=1, frame_token=81,
                    surface_frame_trace_id=value, **SERVICE))
                result = analyze(rows)
                surface = next(row for row in result['source_ack_calls'] if row['role'] == 'surface')
                self.assertTrue(surface['complete_consistent_call'], surface['issues'])
                self.assertEqual(surface['identities']['surface_frame_trace_id'], value)
                self.assertEqual(surface['surface_frame_trace_id_state'], 'unset' if value == -1 else 'captured_signed64')
                observations = [row for row in result['service_ack_observations'] if row['object'] == 'surface_ack']
                self.assertTrue(all(row['surface_frame_trace_id'] == value for row in observations))
                admitted = next(row for row in result['service_ack_observations'] if row['kind'] == 'service.admit')
                self.assertEqual(admitted['surface_frame_trace_id'], value)
                self.assertTrue(admitted['counter_delta_valid'])
                self.assertTrue(result['surface_to_service_links'][0]['accepted'])
                self.assertEqual(result['frame_identity_observations'], [])
                self.assertEqual(result['flow_links'], [])

    def test_surface_trace_id_rejects_values_outside_signed64(self):
        for value in (str(2**63), str(-(2**63)-1), []):
            rows = source_chain()
            rows[1]['args']['bt']['surface_frame_trace_id'] = value
            rows[6]['args']['bt']['surface_frame_trace_id'] = value
            result = analyze(rows)
            self.assertFalse(result['surface_to_service_links'][0]['accepted'])
            self.assertIn('invalid_or_missing_signed64_field', result['summary']['issue_counts'])

    def test_malformed_enqueue_is_a_rival_but_never_accepted(self):
        for field in ('sink_client_id','sink_id','resource_count','entry_kind'):
            with self.subTest(field=field):
                rows = source_chain()
                rival = copy.deepcopy(rows[4])
                rival['ts'] = 41
                rival['args']['bt']['event_seq'] = '2'
                rival['args']['bt']['queue_index'] = '1'
                del rival['args']['bt'][field]
                result = analyze(rows+[rival])
                self.assertFalse(any(row['accepted'] for row in result['service_to_enqueue_links']))
                self.assertTrue(all(row['service_call_candidates'] for row in result['service_to_enqueue_links']))
                rows[4] = rival
                self.assertFalse(analyze(rows)['service_to_enqueue_links'][0]['accepted'])

    def test_ready_sequence_and_internal_clock_order_must_agree(self):
        rows = source_chain()
        rows[2]['args']['bt'].update(event_seq='2', service_pending='0')
        rows[3]['args']['bt']['event_seq'] = '1'
        result = analyze(rows)
        self.assertFalse(result['surface_to_service_links'][0]['accepted'])
        service = next(row for row in result['source_ack_calls'] if row['role'] == 'service')
        self.assertIn('clock_regression_inside_call', service['issues'])

    def test_unique_surface_service_enqueue_and_fifo_chain(self):
        rows = source_chain()+send_batch(seq=2, ts=70)
        result = analyze(rows)
        self.assertTrue(result['surface_to_service_links'][0]['accepted'], result['source_ack_calls'])
        self.assertTrue(result['service_to_enqueue_links'][0]['accepted'])
        self.assertEqual(result['batches'][0]['members'][0]['enqueue_index'], 4)
        self.assertTrue(all(row['protocol_ack_frame_id'] is None for row in result['source_ack_calls']))

    def test_incomplete_and_unidentified_source_rivals_poison_unique_join(self):
        for kind, ts, fields in (
                ('service.ack.emit',35,dict(callback_id=2, pending_before=1,pending_after=0,route='ack',resource_count=0)),
                ('service.ack.done',45,dict(callback_id=2)),
                ('service.ack.emit',35,dict(pending_before=1,pending_after=0,route='ack',resource_count=0))):
            with self.subTest(kind=kind, fields=fields):
                rows = source_chain()+[event(kind, 1, ts=ts, obj='service_sink', instance=2,
                                            service_pending=0, **SERVICE, **fields)]
                result = analyze(rows)
                self.assertFalse(result['service_to_enqueue_links'][0]['accepted'])
                self.assertGreater(len(result['service_to_enqueue_links'][0]['service_call_candidates']), 1)
                self.assertFalse(result['surface_to_service_links'][0]['accepted'])

    def test_overlapping_or_open_surface_rival_blocks_ownership(self):
        for include_end in (False, True):
            rows = source_chain()+[event('surface.ack.ready', 1, ts=8, obj='surface_ack', instance=9, **SURFACE)]
            if include_end:
                rows.append(event('surface.ack.done', 2, ts=62, obj='surface_ack', instance=9, **SURFACE))
            result = analyze(rows)
            self.assertFalse(any(row['accepted'] for row in result['surface_to_service_links']))

    def test_reordered_or_duplicate_service_boundaries_are_not_accepted(self):
        rows = source_chain()
        rows[5]['ts'] = 25
        self.assertFalse(analyze(rows)['service_to_enqueue_links'][0]['accepted'])
        rows = source_chain()+[copy.deepcopy(source_chain()[3])]
        self.assertFalse(analyze(rows)['service_to_enqueue_links'][0]['accepted'])

    def test_extra_ready_or_wrong_address_prevents_surface_join(self):
        rows = source_chain()+[event('service.ack.ready', 1, ts=25, obj='service_sink', instance=2,
                                     service_pending=1, **SERVICE)]
        self.assertFalse(analyze(rows)['surface_to_service_links'][0]['accepted'])
        rows = source_chain()
        rows[1]['args']['bt']['client_address'] = '999'
        rows[6]['args']['bt']['client_address'] = '999'
        self.assertFalse(analyze(rows)['surface_to_service_links'][0]['accepted'])

    def test_wrong_enqueue_sink_route_or_resource_count_is_not_joined(self):
        for field, value in (('sink_id','999'), ('entry_kind','reclaim'), ('resource_count','2')):
            rows = source_chain()
            rows[4]['args']['bt'][field] = value
            self.assertFalse(analyze(rows)['service_to_enqueue_links'][0]['accepted'])

    def test_one_source_call_cannot_claim_two_queue_entries(self):
        rows = source_chain()
        rows.append(event('bundle.enqueue', 2, ts=41, obj='service_group', entry_kind='ack', queue_index=1,
                          sink_client_id=5, sink_id=6, resource_count=0))
        self.assertFalse(any(row['accepted'] for row in analyze(rows)['service_to_enqueue_links']))

    def test_intervening_service_admission_explains_changed_done_pending(self):
        rows = source_chain()
        rows[2]['args']['bt']['service_pending'] = '2'
        rows[3]['args']['bt'].update(service_pending='1', pending_before='2', pending_after='1')
        rows[5]['args']['bt'].update(event_seq='4', service_pending='2')
        rows.append(event('service.admit', 3, ts=35, obj='service_sink', service_pending=2,
                          pending_before=1,pending_after=2,frame_token=81,surface_frame_trace_id=901, **SERVICE))
        result = analyze(rows)
        self.assertTrue(result['service_to_enqueue_links'][0]['accepted'], result['source_ack_calls'])

    def test_null_surface_client_expects_no_service_callback(self):
        fields = dict(SURFACE, client_address=0, client_present=False, called=False)
        rows = [event('surface.ack.ready', 1, ts=10, obj='surface_ack', **fields),
                event('surface.ack.done', 2, ts=11, obj='surface_ack', **fields)]
        self.assertEqual(analyze(rows)['surface_to_service_links'][0]['status'], 'null_client_no_call_expected')


class AttemptAndStateTests(unittest.TestCase):
    def test_accepted_source_order_and_earlier_guards_are_checked(self):
        rows = accepted()
        for index, seq in ((2,4), (3,5), (4,2), (5,3)):
            rows[index]['args']['bt']['event_seq'] = str(seq)
            rows[index]['ts'] = seq*10
        self.assertIn('accepted_source_stage_order_inconsistent', analyze(rows)['attempts'][0]['issues'])
        for edit, expected in (({'sink_bound':False}, 'earlier_sink_guard_inconsistent_at_entry'),
                               ({'page_visible':False}, 'earlier_visibility_guard_inconsistent_at_entry'),
                               ({'last_frame_id_valid':True,'last_frame_id':'101'}, 'earlier_duplicate_guard_inconsistent_at_entry')):
            rows = accepted()
            rows[1]['args']['bt'].update(edit)
            self.assertIn(expected, analyze(rows)['attempts'][0]['issues'])

    def test_attempt_constants_and_entry_epoch_do_not_silently_change(self):
        for field, value in (('origin','SubmitSingleFrame'), ('pts_us','66667'), ('natural_width','640'),
                             ('previous_height','600'), ('entry_epoch','1')):
            rows = accepted()
            rows[3]['args']['bt'][field] = value
            self.assertFalse(analyze(rows)['attempts'][0]['complete_consistent_attempt'])
        rows = accepted()
        for row in rows:
            if row['args']['bt']['kind'].startswith('submit.'):
                row['args']['bt']['entry_epoch'] = '1'
        self.assertIn('entry_epoch_does_not_match_entry_state', analyze(rows)['attempts'][0]['issues'])

    def test_accepted_and_ack_after_reclaim_reconcile(self):
        result = analyze(accepted()+callback())
        attempt = result['attempts'][0]
        self.assertTrue(attempt['complete_consistent_attempt'], attempt['issues'])
        self.assertTrue(attempt['return_history_reconciled'])
        self.assertEqual(attempt['return_pending'], 1)
        self.assertTrue(result['ack_callbacks'][0]['complete_consistent_callback'])
        self.assertTrue(all(row['history_reconciled_at_record'] for row in result['pending_histories'][0]['points']))
        self.assertIsNone(result['ack_callbacks'][0]['acknowledged_frame_identity'])

    def test_pending_guard_is_explicit_and_consistent(self):
        rows = accepted()+[event('submit.entry', 8, attempt_id=2, unique_frame_id=102, pending_ack=1),
            event('submit.return', 9, attempt_id=2, unique_frame_id=102, pending_ack=1,
                  reason='pending_ack', **dict(OUTPUT, guard_stage='pending_ack'))]
        result = analyze(rows)
        self.assertTrue(result['attempts'][1]['complete_consistent_attempt'])
        self.assertEqual(result['attempts'][1]['reason'], 'pending_ack')
        self.assertEqual(result['attempt_comparison_by_return_reason']['pending_ack']['observed_attempts'], 1)
        rows[-1]['args']['bt']['size_changed'] = True
        self.assertIn('pending_ack_guard_state_inconsistent', analyze(rows)['attempts'][1]['issues'])

    def test_size_change_can_accept_with_existing_pending(self):
        rows = accepted()
        second = accepted()[1:]
        for row in second:
            bt = row['args']['bt']
            bt['event_seq'] = str(int(bt['event_seq'])+7)
            row['ts'] += 70
            bt['attempt_id'] = '2'
            if 'unique_frame_id' in bt:
                bt['unique_frame_id'] = '102'
            bt['pending_ack'] = str(int(bt['pending_ack'])+1)
            if 'size_changed' in bt:
                bt['size_changed'] = True
            if bt['kind'] == 'counter.change':
                bt.update(pending_before='1', pending_after='2')
        result = analyze(rows+second)
        self.assertTrue(result['attempts'][1]['complete_consistent_attempt'])
        self.assertTrue(result['attempts'][1]['return_history_reconciled'])
        self.assertEqual(result['attempts'][1]['return_pending'], 2)

    def test_early_return_fields_preserve_short_circuit(self):
        rows = [event('instance.begin', 0), event('submit.entry', 1),
            event('submit.return', 2, reason='no_sink', guard_stage='sink_visibility', sink_bound=False)]
        self.assertTrue(analyze(rows)['attempts'][0]['complete_consistent_attempt'])
        rows[-1]['args']['bt']['output_computed'] = True
        self.assertFalse(analyze(rows)['attempts'][0]['complete_consistent_attempt'])

    def test_missing_entry_return_and_duplicate_identity_are_retained(self):
        result = analyze(accepted()[2:])
        self.assertIn('missing_entry', result['attempts'][0]['issues'])
        self.assertFalse(result['attempts'][0]['return_history_reconciled'])
        result = analyze(accepted()[:-1])
        self.assertIn('missing_return', result['attempts'][0]['issues'])
        rows = accepted()
        rows[-1]['args']['bt']['unique_frame_id'] = '999'
        self.assertIn('missing_or_conflicting_unique_frame_identity', analyze(rows)['attempts'][0]['issues'])

    def test_gap_invalidates_history_until_explicit_reset(self):
        rows = accepted()
        rows.extend([event('state.after', 9, pending_ack=1, cause='test'),
            event('counter.reset', 10, reset_epoch=1, previous_epoch=0,
                  pending_before=1, pending_after=0, pending_ack=0, cause='context_lost'),
            event('state.after', 11, reset_epoch=1, pending_ack=0, cause='test')])
        result = analyze(rows)
        points = result['pending_histories'][0]['points']
        self.assertFalse(points[-3]['history_reconciled_at_record'])
        self.assertTrue(points[-2]['history_reconciled_at_record'])
        self.assertTrue(points[-1]['history_reconciled_at_record'])
        self.assertIn('instance_sequence_gap', result['summary']['issue_counts'])

    def test_frame_id_only_reset_does_not_advance_counter_epoch(self):
        result = analyze(accepted()+[event('frame_id.reset', 8, pending_ack=1, cause='surface_id')])
        self.assertTrue(result['pending_histories'][0]['points'][-1]['history_reconciled_at_record'])

    def test_reentrant_reset_keeps_attempt_and_entry_epoch(self):
        rows = [event('instance.begin', 0), event('submit.entry', 1),
            event('counter.reset', 2, reset_epoch=1, previous_epoch=0, pending_before=0, pending_after=0, cause='context_lost'),
            event('submit.return', 3, reset_epoch=1, entry_epoch=0, reason='not_visible',
                  surface_visible=False, guard_stage='sink_visibility')]
        attempt = analyze(rows)['attempts'][0]
        self.assertTrue(attempt['crossed_reset_epoch'])
        self.assertEqual(attempt['entry_epoch'], 0)
        self.assertEqual(attempt['observed_epochs'], [0,1])
        self.assertTrue(attempt['complete_consistent_attempt'], attempt['issues'])

    def test_zero_count_ack_has_no_decrement(self):
        result = analyze([event('instance.begin', 0)]+callback(start=1, pending=0))
        self.assertFalse(result['ack_callbacks'][0]['applied'])
        self.assertTrue(result['ack_callbacks'][0]['complete_consistent_callback'])

    def test_counter_before_reclaim_is_rejected(self):
        rows = accepted()+callback()
        rows[-3]['args']['bt']['event_seq'], rows[-2]['args']['bt']['event_seq'] = '10', '9'
        result = analyze(rows)
        self.assertIn('counter_change_not_after_reclaim', result['ack_callbacks'][0]['issues'])

    def test_unrecorded_count_change_duplicate_sequence_and_end_are_visible(self):
        rows = [event('instance.begin', 0), event('state.after', 1, pending_ack=1),
                event('instance.end', 1, pending_ack=1), event('state.after', 2, pending_ack=1)]
        result = analyze(rows)
        self.assertIn('duplicate_instance_sequence', result['summary']['issue_counts'])
        self.assertIn('event_after_instance_end', result['summary']['issue_counts'])
        self.assertFalse(result['pending_histories'][0]['points'][-1]['history_reconciled_at_record'])


class BatchAndIdentityTests(unittest.TestCase):
    def test_actual_mojo_edges_join_batches_with_normalized_string_pids(self):
        method = next(row for row in diagnostics.chromium_mojo_flow.METHODS.values() if row['method'] == 'FlushNotifications')
        nodes = [
            dict(name='mojo::Message::Message', pid=10, tid=11, ts=10, ph='X', dur=1, cat='mojom', args={'flags':0}),
            dict(name='Send mojo message', pid=10, tid=11, ts=15, ph='I', cat='mojom'),
            dict(name='Connector::DispatchMessage', pid=20, tid=21, ts=25, ph='X', dur=20, cat='mojom'),
            dict(name='Receive mojo message', pid=20, tid=21, ts=30, ph='X', dur=10, cat='mojom',
                 args={'chrome_mojo_event_info': {'mojo_interface_tag':method['interface_tag'], 'ipc_hash':method['ipc_hash']}})]
        rows = copy.deepcopy(nodes)
        for index in range(3):
            for phase, node in (('s',nodes[index]), ('f',nodes[index+1])):
                edge = {key:node[key] for key in ('pid','tid','ts')}
                edge.update(ph=phase, cat='mojom.flow', name='message', id=index+1)
                if phase == 'f':
                    edge['bp'] = 'e'
                rows.append(edge)
        for group, pid, tid, times in ((send_batch(batch=3),10,11,(5,6,20)),
                                     (receive_batch(batch=91),20,21,(32,33,38))):
            for row, ts in zip(group,times):
                row.update(pid=pid, tid=tid, ts=ts)
            rows.extend(group)
        result = analyze(rows)
        self.assertEqual(result['summary']['validated_mojo_batch_deliveries'], 1)
        self.assertTrue(all(isinstance(batch['pid'], str) for batch in result['batches']))
        self.assertTrue(result['flow_links'][0]['local_batch_ids_were_not_used_as_wire_identity'])
        self.assertEqual(len(result['flow_links'][0]['evidence']['edges']), 3)
        rows.append(dict(nodes[1], cat=[]))
        poisoned = analyze(rows)
        self.assertEqual(poisoned['summary']['validated_mojo_batch_deliveries'], 0)

    def test_zero_ack_batches_and_skipped_client_remain_explicit(self):
        result = analyze(send_batch(members=('begin_frame',))+receive_batch(members=('begin_frame',)))
        self.assertEqual(len(result['batches']), 2)
        self.assertTrue(all(row['complete_consistent_batch'] for row in result['batches']))
        self.assertTrue(all(row['declared_counts']['ack'] == 0 for row in result['batches']))
        self.assertEqual(result['summary']['validated_mojo_batch_deliveries'], 0)
        received = next(row for row in result['batches'] if row['direction'] == 'receive')
        self.assertFalse(received['members'][0]['client_found'])
        self.assertEqual(received['members'][0]['completion_indices'], [])

    def test_local_queue_membership_is_validated_separately(self):
        rows = [event('instance.begin', 0, obj='service_group'),
                event('bundle.enqueue', 1, obj='service_group', entry_kind='ack', queue_index=0,
                      sink_client_id=5, sink_id=6, resource_count=0)]+send_batch(seq=2)
        result = analyze(rows)
        self.assertEqual(result['batches'][0]['local_queue_membership'], 'validated_local_fifo_membership')
        rows[1]['args']['bt']['sink_id'] = '999'
        self.assertEqual(analyze(rows)['batches'][0]['local_queue_membership'], 'queue_membership_mismatch')

    def test_duplicate_member_indices_and_missing_completion_are_not_valid_batches(self):
        rows = send_batch(members=('ack','ack'))
        rows[2]['args']['bt']['entry_index'] = '0'
        self.assertFalse(analyze(rows)['batches'][0]['complete_consistent_batch'])

    def test_sequence_gap_within_batch_blocks_complete_envelope(self):
        rows = send_batch()
        rows[-1]['args']['bt']['event_seq'] = '99'
        result = analyze(rows)
        self.assertFalse(result['batches'][0]['complete_consistent_batch'])
        self.assertFalse(result['batches'][0]['instance_sequence_contiguous_within_batch'])
        rows = receive_batch(found=True)
        del rows[2]
        self.assertFalse(analyze(rows)['batches'][0]['complete_consistent_batch'])

    def test_callback_link_requires_unique_strict_sink_matched_interval(self):
        rows = receive_batch(found=True, ts=50)
        ack = callback(start=1, pending=0)
        for row, ts in zip(ack, (62,65,68)):
            row['ts'] = ts
        result = analyze(rows+ack)
        member = result['batches'][0]['members'][0]
        self.assertIsNotNone(member['uniquely_nested_callback_enter_index'])
        # Equal-coordinate boundaries are not resolved by arbitrary export order.
        ack[0]['ts'] = 60
        member = analyze(rows+ack)['batches'][0]['members'][0]
        self.assertIsNone(member['uniquely_nested_callback_enter_index'])
        self.assertTrue(member['callback_boundary_ambiguous_enter_indices'])

    def test_invalid_callback_is_not_promoted_by_good_nesting(self):
        rows = receive_batch(found=True, ts=50)
        ack = callback(start=1, pending=0)
        for row, ts in zip(ack, (62,65,68)):
            row['ts'] = ts
        ack[-1]['args']['bt']['applied'] = True
        result = analyze(rows+ack)
        self.assertFalse(result['ack_callbacks'][0]['complete_consistent_callback'])
        self.assertIsNone(result['batches'][0]['members'][0]['uniquely_nested_callback_enter_index'])

    def test_malformed_or_incomplete_receiver_callback_blocks_unique_rival(self):
        for mutation in ('missing_enter','missing_done','missing_exit','missing_id','malformed_pending','missing_sink'):
            with self.subTest(mutation=mutation):
                batch = receive_batch(found=True, ts=50)
                ack = callback(start=1, pending=0)
                rival = callback(start=1, pending=0, instance=2)
                for rows, times in ((ack,(62,65,68)), (rival,(63,66,69))):
                    for row, ts in zip(rows,times):
                        row['ts'] = ts
                if mutation in ('missing_enter','missing_done','missing_exit'):
                    del rival[{'missing_enter':0,'missing_done':1,'missing_exit':2}[mutation]]
                elif mutation == 'missing_id':
                    for row in rival:
                        del row['args']['bt']['callback_id']
                elif mutation == 'malformed_pending':
                    rival[0]['args']['bt']['pending_ack'] = []
                else:
                    for row in rival:
                        del row['args']['bt']['sink_id']
                member = analyze(batch+ack+rival)['batches'][0]['members'][0]
                self.assertIsNone(member['uniquely_nested_callback_enter_index'])
                self.assertTrue(member['callback_incomplete_or_invalid_rival_trace_indices'])
                self.assertIsNone(analyze(batch+rival)['batches'][0]['members'][0]['uniquely_nested_callback_enter_index'])

    def test_receiver_callback_order_thread_resource_and_owner_must_agree(self):
        for mutation in ('clock','thread','resource','sequence','overlap'):
            with self.subTest(mutation=mutation):
                batch = receive_batch(found=True, ts=50)
                ack = callback(start=1, pending=0)
                for row, ts in zip(ack,(62,65,68)):
                    row['ts'] = ts
                if mutation == 'clock':
                    ack[1]['ts'] = 69
                elif mutation == 'thread':
                    ack[1]['tid'] = 999
                elif mutation == 'resource':
                    ack[0]['args']['bt']['resource_count'] = '2'
                elif mutation == 'sequence':
                    ack[-1]['args']['bt']['event_seq'] = '9'
                else:
                    second = copy.deepcopy(batch)
                    for row in second:
                        row['args']['bt']['instance_id'] = '2'
                    batch += second
                result = analyze(batch+ack)
                self.assertTrue(all(member['uniquely_nested_callback_enter_index'] is None
                    for row in result['batches'] for member in row['members']))

    def test_same_local_batch_id_never_supplies_cross_process_identity(self):
        rows = send_batch(batch=3)+receive_batch(batch=3)
        result = analyze(rows)
        self.assertEqual(result['flow_links'], [])
        self.assertIn('cross_process_ack_delivery_not_linked', result['summary']['issue_counts'])

    def test_same_pts_different_unique_ids_and_prepare_wrapper_stay_distinct(self):
        rows = [event('frame.prepare.begin', 0, obj='preparation', unique_frame_id=101, pts_us=50000),
            event('frame.prepare.end', 1, obj='preparation', input_valid=True, input_unique_frame_id=101,
                  input_pts_us=50000, output_valid=True, output_unique_frame_id=202, output_pts_us=50000),
            event('frame.select', 0, obj='selection', unique_frame_id=202, pts_us=50000)]
        result = analyze(rows)
        self.assertEqual([row['unique_frame_id'] for row in result['frame_identity_observations']], [101,202])
        self.assertEqual(result['preparation_identity_edges'][0]['input_unique_frame_id'], 101)
        self.assertEqual(result['preparation_identity_edges'][0]['output_unique_frame_id'], 202)

    def test_cancelled_prepare_and_service_ready_without_callback_id(self):
        rows = [event('frame.prepare.end', 0, obj='preparation', input_valid=True, input_unique_frame_id=101,
                      input_pts_us=50000, output_valid=False),
                event('service.ack.ready', 0, obj='service_sink', pending_ack=1),
                event('service.ack.emit', 1, obj='service_sink', callback_id=1,
                      pending_before=1, pending_after=0, route='reclaim', resource_count=2)]
        result = analyze(rows)
        self.assertIsNone(result['preparation_identity_edges'][0]['output_unique_frame_id'])
        self.assertIsNone(result['service_ack_observations'][0]['callback_id'])
        self.assertTrue(result['service_ack_observations'][1]['counter_delta_valid'])
        self.assertTrue(all(row['acknowledged_frame_identity'] is None for row in result['service_ack_observations']))


class InputTests(unittest.TestCase):
    def test_absent_diagnostic_producers_are_not_a_successful_zero_result(self):
        result = analyze([])
        self.assertTrue(result['summary']['missing_diagnostic_records'])
        self.assertIn('submitter', result['summary']['absent_required_object_roles'])
        self.assertFalse(result['coverage']['complete_required_producer_coverage_proven'])

    def test_malformed_envelope_types_are_rejected_without_crashing(self):
        for field, value in (('cat', []), ('pid', None), ('pid', [1]), ('tid', True), ('ts', float('nan'))):
            with self.subTest(field=field, value=value):
                row = event('instance.begin', 0)
                row[field] = value
                result = analyze([row])
                self.assertEqual(result['summary']['rejected_diagnostic_records'], 1)
        for value in ([], {}, None):
            with self.subTest(object=value):
                row = event('instance.begin', 0)
                row['args']['bt']['object'] = value
                self.assertEqual(analyze([row])['summary']['rejected_diagnostic_records'], 1)

    def test_malformed_body_fields_do_not_reconcile_or_crash(self):
        rows = [event('instance.begin', 0)]
        rows[0]['args']['bt']['sink_valid'] = 'true'
        result = analyze(rows)
        self.assertFalse(result['pending_histories'][0]['points'][0]['history_reconciled_at_record'])
        rows = accepted()
        rows[-1]['args']['bt']['reason'] = []
        self.assertFalse(analyze(rows)['attempts'][0]['complete_consistent_attempt'])
        rows = send_batch()
        rows[1]['args']['bt']['entry_kind'] = []
        self.assertFalse(analyze(rows)['batches'][0]['complete_consistent_batch'])

    def test_64_bit_decimal_identity_is_preserved_and_numeric_payload_rejected(self):
        rows = [event('instance.begin', 0, instance=2**63+7)]
        self.assertEqual(analyze(rows)['instances'][0]['instance_id'], str(2**63+7))
        rows[0]['args']['bt']['instance_id'] = float(2**63+7)
        self.assertEqual(analyze(rows)['summary']['rejected_diagnostic_records'], 1)

    def test_bad_schema_kind_missing_fields_and_wrong_clock_are_explicit(self):
        row = event('instance.begin', 0)
        row['args']['bt']['schema_version'] = True
        self.assertEqual(analyze([row])['summary']['rejected_diagnostic_records'], 1)
        row = event('unexpected.new.kind', 0)
        self.assertIn('unknown_record_kind', analyze([row])['summary']['issue_counts'])
        rows = accepted()
        del rows[-1]['args']['bt']['sink_valid']
        self.assertFalse(analyze(rows)['attempts'][0]['complete_consistent_attempt'])
        with self.assertRaises(ValueError):
            diagnostics.analyze([], {}, ALIGNMENT)

    def test_half_open_alignment_and_invalid_alignment(self):
        result = analyze([event('instance.begin', 0, ts=10), event('instance.end', 1, ts=100)], alignment=ALIGNMENT)
        self.assertEqual([row['window']['nominal'] for row in result['records']], ['inside','after'])
        with self.assertRaises(ValueError):
            analyze([], alignment=dict(ALIGNMENT, validated=False))


if __name__ == '__main__':
    resource_limits()
    unittest.main()
