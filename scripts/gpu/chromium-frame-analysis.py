#!/usr/bin/env python3
"""Offline PTS/frame-construction correlation for one explicitly named trial.

Example: chromium-frame-analysis.py --run-dir RUN --trial on-1
  --observation t2-on-1-completed-observation.json --output-dir FRESH

Reads TRACE (default TRIAL-chromium-trace.json), observation, and
TRIAL-before/after-telemetry.json from RUN. Writes inventory, alignment,
correlations and summary to FRESH, exclusively; never needs /tmp timelines.
The unique-PTS clock proof requires a single video selection thread, no repeated
clip PTS, Chromium's pinned <=100us rVFC clamp, and MONOTONIC trace metadata.
A mismatch aborts instead of guessing. No target access or ACK-state inference.
"""
import argparse
import bisect
import collections
import hashlib
import importlib.util
import json
import pathlib
import re
import sys

spec=importlib.util.spec_from_file_location('trace_audit',str(pathlib.Path(__file__).with_name('chromium-trace-inventory.py')))
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
dist=mod.dist
from chromium_trace_common import (read_json, write_json, checked_path, resource_limits,
                                    parser_identity, trace_events, load_trace)

def pts(event):
    a=event.get('args',{})
    if 'timestamp_us' in a: return int(a['timestamp_us'])
    m=re.search(r'timestamp:(-?\d+)',a.get('frame',''))
    return int(m.group(1)) if m else None
def key(e):return (e['pid'],json.dumps(e.get('id2'),sort_keys=True),str(e.get('id')))

def analyze_trial(trial, inv, timeline, observation, telemetry):
    complete=observation['complete']['payload']
    samples=complete['samples']
    spans=timeline['spans'];ins=timeline['instants']
    setframes=sorted([e for e in spans if e['name']=='VideoFrameCompositor::SetCurrentFrame'],key=lambda e:e['ts'])
    selection_threads = {(e['pid'], e['tid']) for e in setframes}
    if len(selection_threads) != 1:
        raise ValueError('requires exactly one video selection thread; cannot pool instances')
    video_pid, video_tid = next(iter(selection_threads))
    if inv.get('trace_metadata_evidence', {}).get('clock-domain') != 'LINUX_CLOCK_MONOTONIC':
        raise ValueError('requires explicit LINUX_CLOCK_MONOTONIC metadata')
    set_by_pts=collections.defaultdict(list)
    for e in setframes:set_by_pts[pts(e)].append(e)
    if None in set_by_pts or any(len(v) != 1 for v in set_by_pts.values()):
        raise ValueError('selected PTS must be nonempty and unique across the complete clip trace')
    if len(samples) < 2:
        raise ValueError('requires baseline and terminal fixture samples')
    anchors=[]
    for sample in samples:
        v=sample.get('rvfc')
        if not v:continue
        media_pts=round(v['media_time']*1e6)
        candidates=set_by_pts[media_pts]
        if len(candidates)!=1:raise ValueError(f'{trial} ambiguous/missing frame {media_pts}')
        e=candidates[0]
        # rVFC clamps zero-based presentation time with <=100us resolution.
        # Tick read occurs INSIDE this SetCurrentFrame X scope, not at its entry.
        lo=e['ts']-v['presentation_time_ms']*1000-100
        hi=e['end']-v['presentation_time_ms']*1000+100
        anchors.append({'sample_sequence':sample['sequence'],'media_pts_us':media_pts,
                        'presented_frames':v['presented_frames'],'presentation_time_ms':v['presentation_time_ms'],
                        'trace_index':e['index'],'span_us':[e['ts'],e['end']],
                        'document_origin_bound_us':[lo,hi]})
    if not anchors:
        raise ValueError('no matching rVFC clock anchors')
    lo=max(a['document_origin_bound_us'][0] for a in anchors)
    hi=min(a['document_origin_bound_us'][1] for a in anchors)
    if hi<lo:raise ValueError(f'{trial}: incompatible frame anchors {lo},{hi}')
    origin=(lo+hi)/2
    # performance.now clamps absolute now and absolute origin separately,
    # whereas rVFC clamps their difference. Allow another 200us conservatively.
    uncertainty=(hi-lo)/2+200
    start=origin+samples[0]['browser_monotonic_ms']*1000
    end=origin+samples[-1]['browser_monotonic_ms']*1000
    helper=[]
    for phase in ['before','after']:
        t=telemetry[phase]
        a,b,w=t['guest_monotonic_start_ns'],t['guest_monotonic_end_ns'],t['guest_wall_start_ns']
        helper.append({'phase':phase,'monotonic_start_ns':a,'monotonic_end_ns':b,'wall_start_ns':w,
                       'wall_minus_monotonic_ns_bounds':[w-b,w-a]})
    hlow=max(x['wall_minus_monotonic_ns_bounds'][0] for x in helper)
    hhigh=min(x['wall_minus_monotonic_ns_bounds'][1] for x in helper)
    if hlow>hhigh:raise ValueError('helper clocks inconsistent')
    wall_checks=[]
    for phase in ['ready','started','complete']:
        r=observation[phase]['payload']
        offset_ns=r['wall_ms']*1000000-round((origin+r['browser_monotonic_ms']*1000)*1000)
        # Date.now integer milliseconds; report() reads wall then perf with no tighter sampling bracket.
        wall_checks.append({'phase':phase,'wall_ms':r['wall_ms'],'browser_monotonic_ms':r['browser_monotonic_ms'],
                            'derived_wall_minus_monotonic_ns':offset_ns,
                            'inside_helper_interval_with_2ms_reporting_tolerance':hlow-2000000<=offset_ns<=hhigh+2000000})
    if not all(x['inside_helper_interval_with_2ms_reporting_tolerance'] for x in wall_checks):raise ValueError('frame alignment does not agree with helper clock')
    alignment={'validated':True,'method':'Uniquely matched video PTS: rVFC presentation_time belongs inside pinned SetCurrentFrame X scope; intersect all <=100us-clamped anchors, add200us for performance.now separate absolute clamps; cross-check explicit MONOTONIC metadata and helper wall/mono brackets.',
       'evidence':{'frame_anchors':anchors,'unique_frame_anchors':len(set(x['media_pts_us'] for x in anchors)),
                   'document_origin_intersection_us':[lo,hi],'helper_clock_brackets':helper,'wall_checks':wall_checks,
                   'trace_clock_domain':inv['trace_metadata_evidence']['clock-domain'],
                   'source_files':['video_frame_compositor.cc:177-187,328-347','video_frame_callback_requester_impl.cc:219-234,309-325','performance.cc:1296-1309','time_now_posix.cc:100-119']},
       'trace_start_us':start,'trace_end_us':end,'uncertainty_us':uncertainty,
       'endpoint_limits':'Clock uncertainty only; fixture baseline/terminal getters have their separately recorded collection timing. This mapping does not equate rVFC with physical scanout.'}
    def inside(t):return start<=t<=end
    def rows(name):return [e for e in spans if e['name']==name]
    def metrics(seq):
        seq=sorted([e for e in seq if inside(e['ts'])],key=lambda e:e['ts'])
        closed=[e for e in seq if e['end']<=end]
        return {'starts':len(seq),'closed':len(closed),'duration_us':dist([e['end']-e['ts'] for e in closed]),
                'start_gap_us':dist([b['ts']-a['ts'] for a,b in zip(seq,seq[1:])])}
    prep=collections.defaultdict(list)
    for e in rows('VideoDecoderStream::PrepareOutput'):
        if e['pid'] == video_pid:
            prep[pts(e)].append(e)
    selected=[e for e in setframes if inside(e['ts'])]
    readiness=[]
    for e in selected:
        prior=[p for p in prep[pts(e)] if p['end']<=e['ts']]
        if len(prior)!=1:raise ValueError('no unique prior prepared frame')
        p=prior[0]
        readiness.append({'media_pts_us':pts(e),'prepare_end_us':p['end'],'set_current_us':e['ts'],
                          'already_prepared_us':e['ts']-p['end'],'set_trace_index':e['index'],'prepare_trace_index':p['index']})
    # Only accepted submissions start the Pre-submit buffering named span.
    # Join its END to the containing SubmitFrame call, NOT any attempted call.
    attempts=sorted(rows('VideoFrameSubmitter::SubmitFrame'),key=lambda e:e['ts'])
    if any((e['pid'], e['tid']) != (video_pid, video_tid) for e in attempts):
        raise ValueError('multiple submission threads cannot be joined by PTS')
    if any(b['ts'] < a['end'] for a,b in zip(attempts, attempts[1:])):
        raise ValueError('overlapping submission attempts are ambiguous')
    attempt_times=[e['ts'] for e in attempts]
    presented={key(e)+(e['ts'],):e for e in rows('VideoFrameSubmitter')}
    accepted=[];unmatched=[]
    for e in rows('Pre-submit buffering'):
        pos=bisect.bisect_right(attempt_times,e['end'])-1
        if pos<0 or not(attempts[pos]['ts']<=e['end']<=attempts[pos]['end']):
            unmatched.append(e);continue
        attempt=attempts[pos]
        if attempt['pid']!=e['pid'] or attempt['tid']!=e['tid']:raise ValueError('submission thread mismatch')
        outer=presented.get(key(e)+(e['ts'],))
        accepted.append({'media_pts_us':pts(attempt),'submit_us':e['end'],'decode_end_us':e['ts'],
                         'presentation_us':outer['end'] if outer else None,
                         'submit_to_reported_presentation_us':outer['end']-e['end'] if outer else None,
                         'pre_submit_buffering_us':e['end']-e['ts'],
                         'submit_trace_index':attempt['index'],'prebuffer_trace_index':e['index']})
    if unmatched:raise ValueError('prebuffer not contained in SubmitFrame')
    # Independent unconditional accepted-branch evidence: each video-frame
    # CreateCompositorFrame calls AppendQuads after all SubmitFrame guards.
    append_attempts=set();append_records=[]
    for e in rows('VideoFrameResourceProvider::AppendQuads'):
        pos=bisect.bisect_right(attempt_times,e['ts'])-1
        if pos<0 or not(attempts[pos]['ts']<=e['ts']<=e['end']<=attempts[pos]['end']):
            raise ValueError('AppendQuads outside a SubmitFrame call')
        if (e['pid'],e['tid']) != (video_pid,video_tid):
            raise ValueError('AppendQuads submission thread mismatch')
        append_attempts.add(attempts[pos]['index'])
        append_records.append({'append_trace_index':e['index'],'submit_trace_index':attempts[pos]['index'],'media_pts_us':pts(attempts[pos]),'append_scope_us':[e['ts'],e['end']]})
    if append_attempts!={a['submit_trace_index'] for a in accepted}:
        raise ValueError('conditional prebuffer coverage differs from unconditional AppendQuads')
    inaccepted=[a for a in accepted if inside(a['submit_us'])]
    accepted_pts={a['media_pts_us'] for a in accepted}
    selected_not_accepted=[e for e in selected if pts(e) not in accepted_pts]
    lost_details=[]
    for e in selected_not_accepted:
        calls=[a for a in attempts if pts(a)==pts(e)]
        if not calls:raise ValueError('selected frame never attempted')
        prior=[a for a in accepted if a['submit_us']<e['ts']]
        future=[a for a in accepted if a['submit_us']>e['ts']]
        prev=max(prior,key=lambda a:a['submit_us']) if prior else None
        nxt=min(future,key=lambda a:a['submit_us']) if future else None
        ready=next(r for r in readiness if r['media_pts_us']==pts(e))
        lost_details.append({'media_pts_us':pts(e),'set_current_us':e['ts'],
          'already_prepared_us':ready['already_prepared_us'],
          'submit_attempts':[{'trace_index':a['index'],'start_us':a['ts'],'duration_us':a['end']-a['ts']} for a in calls],
          'previous_accepted':prev,'next_accepted':nxt,
          'adjacent_accepted_gap_us':nxt['submit_us']-prev['submit_us'] if prev and nxt else None})
    render=sorted(rows('VideoRendererImpl::Render'),key=lambda e:e['ts'])
    drop_events=[e for e in ins if e['name']=='VideoFramesDropped']
    drop_windows=[]
    for i in range(1,len(samples)):
        a,b=samples[i-1],samples[i];left=origin+a['browser_monotonic_ms']*1000;right=origin+b['browser_monotonic_ms']*1000
        if b['dropped_frames']<=a['dropped_frames']:continue
        render_starts=[e['ts'] for e in render if left<=e['ts']<=right]
        drop_windows.append({'sample_start_ms':a['elapsed_ms'],'sample_end_ms':b['elapsed_ms'],
          'counter_delta':b['dropped_frames']-a['dropped_frames'],'render_starts':len(render_starts),
          'render_internal_gap_max_us':max([y-x for x,y in zip(render_starts,render_starts[1:])],default=None),
          'selected_but_never_submitted':[pts(e) for e in selected_not_accepted if left<=e['ts']<=right],
          'drop_stats_updates_count':sum(e['args'].get('count',0) for e in drop_events if left<=e['ts']<=right)})
    sensitivity=[]
    center_pts={pts(e) for e in selected}
    for sa,ea in [(-1,-1),(-1,1),(1,-1),(1,1)]:
        left=start+sa*uncertainty;right=end+ea*uncertainty
        cohort={pts(e) for e in setframes if left<=e['ts']<=right}
        sensitivity.append({'start_shift_us':sa*uncertainty,'end_shift_us':ea*uncertainty,
           'selected_count':len(cohort),'admitted_cohort_count':len(cohort & accepted_pts),
           'never_constructed_count':len(cohort - accepted_pts),
           'drop_stat_count':sum(e['args'].get('count',0) for e in drop_events if left<=e['ts']<=right),
           'added_pts_us':sorted(cohort-center_pts),'removed_pts_us':sorted(center_pts-cohort)})
    stats=inv['trace_metadata_evidence']['trace_processor_stats']
    lost={k:v for k,v in stats.items() if ('loss' in k or 'failure' in k or 'parser_error' in k or 'skipped' in k or 'dropped' in k) and isinstance(v,(int,float)) and v}
    result={'trace_input':inv['input'],'alignment':{'start_us':start,'end_us':end,'uncertainty_us':uncertainty,'anchor_count':len(anchors),'unique_anchors':len(set(a['media_pts_us'] for a in anchors))},
      'coverage':{'buffers':stats['traced_buf'],'nonzero_loss_failure_counters':lost,'pairing':{k:v for k,v in inv['pairing'].items() if k.endswith('count')},
                  'relevant_thread_bounds':[r for r in inv['thread_roles'] if r['thread_name'] in ('Media','VideoFrameCompositor','VizCompositorThread','CrGpuMain')],
                  'open_media_spans':[x for x in inv['pairing']['pending_begins'] if x['cat']=='media']},
      'endpoint_sensitivity':sensitivity,
      'legacy':complete['legacy'],'whole_window_delta':complete['whole_window_delta'],
      'metrics':{n:metrics(rows(n)) for n in ['VideoDecoderStream::Decode','VideoDecoderStream::Read','VideoDecoderStream::PrepareOutput','VideoRendererImpl::Render','VideoFrameCompositor::SetCurrentFrame','VideoFrameSubmitter::OnBeginFrame','VideoFrameSubmitter::SubmitFrame','Display::DrawAndSwap','Graphics.Pipeline.DrawAndSwap','SkiaOutputSurfaceImplOnGpu::SwapBuffers']},
      'prepared_to_set_us':dist([r['already_prepared_us'] for r in readiness]),
      'selected_frames':len(selected),'admitted_submissions_in_window':len(inaccepted),
      'selected_cohort_admitted_count':len(selected)-len(selected_not_accepted),
      'selected_cohort_admitted_pts_us':[pts(e) for e in selected if pts(e) in accepted_pts],
      'frame_identity':'Renderer PID plus unique clip media timestamp_us; actual VideoFrame::unique_id is not emitted in this trace.',
      'unconditional_append_quads_matches_prebuffer':True,
      'lost_selected_prepared_lead_us':dist([r['already_prepared_us'] for r in lost_details]),
      'lost_selected_attempt_duration_us':dist([a['duration_us'] for r in lost_details for a in r['submit_attempts']]),
      'lost_selected_neighbor_accepted_gap_us':dist([r['adjacent_accepted_gap_us'] for r in lost_details if r['adjacent_accepted_gap_us'] is not None]),
      'selected_never_constructed_count':len(selected_not_accepted),'selected_never_constructed_pts_us':[pts(e) for e in selected_not_accepted],
      'admitted_decode_to_prebuffer_marker_us':dist([a['pre_submit_buffering_us'] for a in inaccepted]),
      'admitted_prebuffer_marker_to_reported_presentation_us':dist([a['submit_to_reported_presentation_us'] for a in inaccepted if a['submit_to_reported_presentation_us'] is not None]),
      'drop_stat_count_whole_trace':sum(e['args'].get('count',0) for e in drop_events),
      'drop_stat_count_inside_window':sum(e['args'].get('count',0) for e in drop_events if inside(e['ts'])),
      'drop_windows':drop_windows,
      'limits':['Prepared-to-set measures frames that reached SetCurrentFrame, so selection bias prevents excluding decoder starvation of skipped frames.',
                'SubmitFrame includes early-return attempts; named Pre-submit buffering and unconditional nested AppendQuads identify the same calls admitted past all false-return guards. The prebuffer marker precedes resource preparation and the actual SubmitCompositorFrame call.',
                'Named-track outer endpoints are Chromium presentation feedback timestamps; Linux deliberately ignores failure flag. Neither these endpoints nor frame-construction admission prove ACK receipt or physical scanout.',
                'Source drop counters are batched; missing accepted submissions are an observable stage transition, not an exact reconstruction of the legacy drop counter.',
                'Elapsed spans include scheduling and nested work, not CPU service or kernel-lock waits.']}
    correlations = {'readiness':readiness,'admitted_submissions':accepted,
                    'unconditional_append_quads':append_records,
                    'lost_selected_details':lost_details,'alignment':alignment}
    return result, correlations, alignment


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run-dir', required=True)
    ap.add_argument('--trial', required=True)
    ap.add_argument('--observation', required=True, help='JSON path within run directory')
    trace_group = ap.add_mutually_exclusive_group()
    trace_group.add_argument('--trace', help='JSON path within run directory; default TRIAL-chromium-trace.json')
    trace_group.add_argument('--trace-manifest', help='Explicit complete event-shard manifest within run directory')
    ap.add_argument('--output-dir', required=True, help='Separate directory; existing output files are refused')
    args = ap.parse_args()
    resource_limits()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}', args.trial):
        raise ValueError('invalid trial label')
    root, output = checked_path(args.run_dir), checked_path(args.output_dir)
    if root == output:
        raise ValueError('output directory must differ from source run directory')
    output.mkdir(parents=True, exist_ok=True)
    identities = {}
    def load_name(name, trace_manifest=None):
        path = checked_path(root / name)
        if not path.is_relative_to(root):
            raise ValueError('input overrides must remain inside run directory')
        value, identity = (read_json(path) if trace_manifest is None
                           else load_trace(path,manifest=trace_manifest))
        identities[name] = identity
        return value
    trace_name = args.trace_manifest or args.trace or args.trial + '-chromium-trace.json'
    trace = load_name(trace_name,trace_manifest=bool(args.trace_manifest))
    events = trace_events(trace)
    meta = {k:v for k,v in trace.items() if k != 'traceEvents'} if isinstance(trace,dict) else {}
    timeline = {}
    inv = mod.analyze(events, meta, None, timeline)
    inv['input'] = identities[trace_name]
    observation = load_name(args.observation)
    telemetry = {phase:load_name(args.trial + '-' + phase + '-telemetry.json')
                 for phase in ('before', 'after')}
    report, correlations, alignment = analyze_trial(args.trial, inv, timeline, observation, telemetry)
    # Recompute coverage using the independently validated frame/fixture interval.
    aligned = mod.analyze(events, meta, alignment)
    aligned['input'] = identities[trace_name]
    report['coverage']['pairing'] = {k:v for k,v in aligned['pairing'].items() if k.endswith('count')}
    report['inputs'] = identities
    report['parser_inputs'] = parser_identity(__file__)
    report['parser_inputs'].update(mod.parser_identity(mod.__file__))
    prefix = output / args.trial
    write_json(str(prefix) + '-trace-inventory.json', aligned)
    write_json(str(prefix) + '-trace-alignment.json', alignment)
    write_json(str(prefix) + '-frame-correlations.json', correlations)
    write_json(str(prefix) + '-frame-analysis.json', report)
    print(json.dumps({'trial':args.trial, 'output_dir':str(output),
                     **{k:report[k] for k in ('alignment','selected_frames',
                     'selected_cohort_admitted_count','selected_never_constructed_count',
                     'drop_stat_count_inside_window')}}))


if __name__ == '__main__':
    main()
