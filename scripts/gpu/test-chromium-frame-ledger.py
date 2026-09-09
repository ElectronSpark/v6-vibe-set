#!/usr/bin/env python3
"""Small, offline regressions for frame-ledger identity and absence handling."""
import importlib.util
import pathlib
import unittest

from chromium_trace_common import resource_limits

spec = importlib.util.spec_from_file_location(
    'ledger', pathlib.Path(__file__).with_name('chromium-frame-ledger.py'))
ledger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ledger)

PREPARE, SELECT, ATTEMPT, APPEND, PREBUFFER, PRESENT = ledger.STAGES
ALIGN = dict(validated=True, method='synthetic fixture', evidence={'test': True},
             trace_start_us=10, trace_end_us=30, uncertainty_us=0)
META = {'metadata': {'clock-domain': 'LINUX_CLOCK_MONOTONIC',
                     'trace_processor_stats': {'traced_buf': []}}}


def event(name, ts, dur=1, pid=1, tid=2, pts=None, ph='X', args=None, **fields):
    arguments = dict(args or {})
    if pts is not None:
        arguments['timestamp_us'] = pts
    return dict(name=name, ts=ts, dur=dur, pid=pid, tid=tid, ph=ph,
                cat='media', args=arguments, **fields)


def accepted_events(pts=1000, pid=1, tid=2):
    return [event(PREPARE, 1, 2, pid=pid, tid=3, pts=pts),
            event(SELECT, 10, pid=pid, tid=tid, pts=pts),
            event(ATTEMPT, 11, pid=pid, tid=tid, pts=pts),
            event(ATTEMPT, 20, 8, pid=pid, tid=tid, pts=pts),
            event(APPEND, 23, pid=pid, tid=tid),
            event(PREBUFFER, 3, 20, pid=pid, tid=tid, id='frame-1'),
            event(PRESENT, 3, 37, pid=pid, tid=tid, id='frame-1')]


def analyze(events, alignment=ALIGN, metadata=META):
    return ledger.analyze(events, metadata, alignment)


def stage(report, name):
    return [row for row in report['occurrences'] if row['stage'] == name]


class LedgerTests(unittest.TestCase):
    def test_repeated_attempts_and_later_feedback_remain_distinct(self):
        result = analyze(accepted_events())
        self.assertEqual(len(result['occurrences']), 7)
        self.assertEqual(len(result['candidate_cohorts']), 1)
        attempts = stage(result, 'attempt')
        self.assertEqual([row['construction_markers'] for row in attempts], ['none_observed', 'both'])
        construction = stage(result, 'construction')[0]
        self.assertEqual(construction['pts_us'], 1000)
        self.assertEqual(construction['reported_presentation']['status'], 'known_endpoint')
        presentation = stage(result, 'reported_presentation')[0]
        self.assertEqual(presentation['end_us'], 40)
        self.assertEqual(presentation['end_window']['nominal'], 'after')
        self.assertFalse(construction['reported_presentation']['physical_scanout_proven'])
        self.assertIsNone(result['candidate_cohorts'][0]['confirmed_drop_attribution'])

    def test_prepared_unselected_is_candidate_and_open_prepare_is_not_completed(self):
        result = analyze([event(PREPARE, 12, pts=1000),
                          event(PREPARE, 14, pts=2000, ph='B')])
        self.assertEqual(result['summary']['prepared_without_selection_candidate_buckets'], 1)
        self.assertEqual(result['summary']['unclosed_unlinked_or_invalid_stage_occurrences'], 1)
        for cohort in result['candidate_cohorts']:
            self.assertTrue(cohort['absence_is_limited_to_retained_trace'])
            self.assertIsNone(cohort['confirmed_drop_attribution'])

    def test_missing_attempt_is_retained_without_aborting(self):
        result = analyze([event(PREPARE, 1, pts=1000), event(SELECT, 15, pts=1000)])
        labels = result['candidate_cohorts'][0]['labels']
        self.assertIn('selected_without_observed_attempt', labels)
        self.assertIn('selected_without_observed_construction', labels)

    def test_open_attempt_does_not_count_as_completed(self):
        result = analyze([event(SELECT, 12, pts=1000), event(ATTEMPT, 14, pts=1000, ph='B')])
        labels = result['candidate_cohorts'][0]['labels']
        self.assertIn('selected_without_completed_attempt', labels)
        self.assertNotIn('selected_without_observed_attempt', labels)

    def test_missing_feedback_does_not_mean_dropped(self):
        result = analyze(accepted_events()[:-1])
        construction = stage(result, 'construction')[0]
        self.assertEqual(construction['reported_presentation']['status'], 'missing')
        self.assertIsNone(result['candidate_cohorts'][0]['confirmed_drop_attribution'])

    def test_unclosed_feedback_is_censored_not_known_endpoint(self):
        events = accepted_events()
        events[-1]['ph'] = 'b'
        result = analyze(events)
        construction = stage(result, 'construction')[0]
        self.assertEqual(construction['reported_presentation']['status'], 'unclosed_or_invalid_feedback_scope')
        self.assertIsNone(stage(result, 'reported_presentation')[0]['end_us'])

    def test_end_only_feedback_stays_unlinked(self):
        events = accepted_events()[:-1]
        events.append(event(PRESENT, 40, ph='e', id='frame-1'))
        result = analyze(events)
        feedback = stage(result, 'reported_presentation')[0]
        self.assertEqual(feedback['scope_state'], 'no_valid_begin_link')
        self.assertEqual(feedback['prebuffer_ids'], [])
        self.assertEqual(stage(result, 'construction')[0]['reported_presentation']['status'], 'missing')

    def test_half_open_window_and_uncertainty_are_separate(self):
        result = analyze([event(PREPARE, 9, pts=1), event(PREPARE, 10, pts=2),
                          event(PREPARE, 29, pts=3), event(PREPARE, 30, pts=4)],
                         dict(ALIGN, uncertainty_us=1))
        self.assertEqual([row['start_window']['nominal'] for row in result['occurrences']],
                         ['before', 'inside', 'inside', 'after'])
        self.assertTrue(all(row['start_window']['boundary_uncertain'] for row in result['occurrences']))
        self.assertEqual(result['summary']['stage_occurrences_starting_in_nominal_window']['preparation'], 2)

    def test_same_pts_different_processes_never_join(self):
        result = analyze([event(PREPARE, 12, pts=1000, pid=1),
                          event(SELECT, 15, pts=1000, pid=9)])
        self.assertEqual(len(result['candidate_cohorts']), 2)
        self.assertEqual(result['summary']['prepared_without_selection_candidate_buckets'], 1)
        self.assertTrue(all(len(row['producer_tids']) == 1 for row in result['candidate_cohorts']))

    def test_repeated_pts_and_multiple_selection_threads_are_explicit(self):
        result = analyze([event(PREPARE, 1, pts=1000), event(PREPARE, 3, pts=1000),
                          event(SELECT, 10, pts=1000), event(SELECT, 12, pts=1000, tid=8)])
        cohort = result['candidate_cohorts'][0]
        self.assertEqual(len(cohort['occurrence_ids_by_stage']['selection']), 2)
        self.assertIn('repeated_pts_scopes_not_unique_frame_identity', cohort['labels'])
        self.assertIn('multiple_selection_threads_process_stream_ambiguous', cohort['labels'])

    def test_missing_and_conflicting_pts_do_not_collapse(self):
        result = analyze([event(PREPARE, 12), event(PREPARE, 14),
                          event(PREPARE, 16, pts=1000, args={'frame': 'timestamp:2000'}),
                          event(PREPARE, 18, args={'timestamp_us': 1.5})])
        self.assertEqual(len(result['candidate_cohorts']), 4)
        self.assertEqual([row['pts_status'] for row in result['occurrences']],
                         ['missing', 'missing', 'conflicting', 'invalid'])
        self.assertTrue(all(row['unresolved_occurrence_id'] for row in result['candidate_cohorts']))

    def test_ambiguous_containment_and_shared_boundary_are_not_guessed(self):
        for attempts, append in [
                ([event(ATTEMPT, 10, 20, pts=1000), event(ATTEMPT, 12, 10, pts=2000)], event(APPEND, 15)),
                ([event(ATTEMPT, 10, 10, pts=1000), event(ATTEMPT, 20, 10, pts=2000)], event(APPEND, 20, 0))]:
            with self.subTest(append=append):
                result = analyze(attempts+[append])
                construction = stage(result, 'construction')[0]
                self.assertEqual(construction['attempt_link'], 'ambiguous')
                self.assertEqual(len(construction['attempt_ids']), 2)
                self.assertIsNone(construction['pts_us'])
                self.assertEqual(construction['reported_presentation']['status'], 'construction_attempt_link_unresolved')

    def test_duplicate_feedback_identity_is_not_last_write_wins(self):
        events = accepted_events()
        events.append(dict(events[-1], dur=40))
        result = analyze(events)
        self.assertEqual(len(stage(result, 'reported_presentation')), 2)
        self.assertEqual(stage(result, 'construction')[0]['reported_presentation']['status'], 'ambiguous')
        self.assertTrue(all(row['pts_us'] is None for row in stage(result, 'reported_presentation')))

    def test_drop_counter_producers_and_invalid_counts_stay_separate(self):
        result = analyze([event(SELECT, 12, pts=1000),
            event('VideoFramesDropped', 14, ph='I', args={'count': 4}),
            event('VideoFramesDropped', 30, ph='I', args={'count': 5}),
            event(PREPARE, 10, pts=2000, pid=9),
            event('VideoFramesDropped', 16, ph='I', pid=9, args={'count': 3}),
            event('VideoFramesDropped', 18, ph='I', pid=9, args={'count': -1})])
        producers = result['drop_counter_producers']
        self.assertEqual([row['valid_count_sum_nominal_window'] for row in producers], [4, 3])
        self.assertEqual(producers[1]['attribution'], 'outside_selected_processes')
        self.assertEqual(producers[1]['invalid_count_updates'], 1)
        self.assertTrue(result['summary']['multiple_drop_producers'])
        self.assertEqual(result['summary']['drop_producers_outside_selected_processes'], 1)
        self.assertTrue(result['summary']['drop_stream_attribution_unresolved'])
        self.assertNotIn('remaining_drops', result['summary'])

    def test_single_counter_producer_without_selection_is_unresolved(self):
        result = analyze([event('VideoFramesDropped', 14, ph='I', args={'count': 4})])
        self.assertFalse(result['summary']['multiple_drop_producers'])
        self.assertEqual(result['summary']['drop_producers_outside_selected_processes'], 1)
        self.assertTrue(result['summary']['drop_stream_attribution_unresolved'])

    def test_target_open_records_are_not_limited_by_inventory_preview(self):
        result = analyze([event(PREPARE, index, pts=index, ph='B') for index in range(205)])
        self.assertEqual(len(result['occurrences']), 205)
        self.assertEqual(result['coverage']['pairing']['pending_begin_count'], 205)
        self.assertTrue(result['coverage']['pairing_previews_truncated']['pending_begins'])
        self.assertEqual(result['summary']['prepared_without_selection_candidate_buckets'], 0)

    def test_loss_markers_stats_and_unmatched_ends_are_exposed(self):
        metadata = {'metadata': dict(META['metadata'], trace_processor_stats={
            'traced_buf': [{'chunks_overwritten': 2}], 'parser_errors': 1})}
        result = analyze([event('data_loss', 9, ph='I'), event(ATTEMPT, 15, ph='E', pts=1000)], metadata=metadata)
        self.assertEqual(len(result['coverage']['explicit_loss_markers']), 1)
        self.assertEqual(len(result['coverage']['nonzero_loss_or_failure_stats']), 2)
        self.assertEqual(result['occurrences'][0]['scope_state'], 'no_valid_begin_link')
        self.assertFalse(result['coverage']['complete_producer_coverage_proven'])

    def test_alignment_and_clock_evidence_are_required(self):
        for alignment in (None, dict(ALIGN, validated=False), dict(ALIGN, trace_end_us=1)):
            with self.subTest(alignment=alignment), self.assertRaises(ValueError):
                analyze([], alignment=alignment)
        with self.assertRaisesRegex(ValueError, 'MONOTONIC'):
            analyze([], metadata={})


if __name__ == '__main__':
    resource_limits()
    unittest.main()
