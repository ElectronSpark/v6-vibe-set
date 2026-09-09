#!/usr/bin/env python3
"""Bounded, offline Chrome JSON inventory and span analysis (no target access).

An optional alignment JSON supplies independently established trace_start_us,
trace_end_us, uncertainty_us, method, evidence, and validated=true. This program
does NOT derive a clock mapping from similarly sized timestamps. It records the
mapping verbatim and checks temporal coverage, not completeness of the producer.
"""
import argparse
import collections
import hashlib
import json
import math
import pathlib
import re
import statistics

from chromium_trace_common import (read_json, write_json, number, resource_limits,
                                    parser_identity, trace_events, load_trace)

INTEREST = re.compile(r"video|decode|render|begin.?frame|display|swap|present|draw|drop|commit|composit|submit|buffer|underflow|starv", re.I)


def dist(values):
    if not values:
        return {"count": 0}
    v = sorted(values)
    def pct(q):
        pos = (len(v) - 1) * q
        lo = math.floor(pos)
        return v[lo] + (v[min(lo + 1, len(v) - 1)] - v[lo]) * (pos - lo)
    return {"count": len(v), "min": v[0], "mean": statistics.fmean(v),
            "p50": pct(.5), "p95": pct(.95), "p99": pct(.99), "max": v[-1]}


def compact(value, limit=4096):
    encoded = json.dumps(value, ensure_ascii=False)
    return value if len(encoded) <= limit else {"truncated_preview": encoded[:limit], "original_characters": len(encoded)}


def analyze(events, root_metadata, alignment, timeline=None):
    if not isinstance(events, list) or len(events) > 1_500_000:
        raise ValueError("traceEvents must be a list of at most 1.5 million events")
    proc_names, thread_names, counts = {}, {}, collections.Counter()
    phases, categories = collections.Counter(), collections.Counter()
    stacks, async_stacks = collections.defaultdict(list), collections.defaultdict(list)
    spans, instants, unmatched_ends, malformed = [], [], [], []
    clocks, lost_markers, thread_times = [], [], collections.defaultdict(list)
    timestamps = []
    for index, e in enumerate(events):
        if not isinstance(e, dict):
            malformed.append({"index": index, "reason": "non-object event"})
            continue
        ph, name, cat = e.get("ph", ""), e.get("name", ""), e.get("cat", "")
        pid, tid = str(e.get("pid", "?")), str(e.get("tid", "?"))
        ts, args = e.get("ts"), e.get("args", {})
        phases[ph] += 1
        counts[(cat, name, ph, pid, tid)] += 1
        for category in cat.split(","):
            categories[category] += 1
        if ph == "M":
            if name == "process_name":
                proc_names[pid] = args.get("name", "")
            elif name == "thread_name":
                thread_names[(pid, tid)] = args.get("name", "")
        if re.search(r"clock.?sync|clock.?snapshot|time.?ticks|unix.?epoch", name, re.I):
            clocks.append({"index": index, "event": compact(e)})
        if re.search(r"overflow|lost.?event|data.?loss|buffer.?wrap", name, re.I):
            lost_markers.append({"index": index, "event": compact(e)})
        if not number(ts):
            continue
        timestamps.append(ts)
        if ph != "M":
            thread_times[(pid, tid)].append(ts)
        item = {"index": index, "pid": pid, "tid": tid, "name": name,
                "cat": cat, "ts": ts, "args": args, "ph": ph,
                "id": e.get("id"), "id2": e.get("id2"), "scope": e.get("scope")}
        if ph == "X":
            dur = e.get("dur")
            if not number(dur) or dur < 0:
                malformed.append({"index": index, "reason": "invalid complete duration"})
            else:
                spans.append(dict(item, end=ts + dur, end_index=index, end_args={}, pairing="X"))
        elif ph == "B":
            stacks[(pid, tid)].append(item)
        elif ph == "E":
            stack = stacks[(pid, tid)]
            if not stack:
                unmatched_ends.append(item)
            elif name and stack[-1]["name"] != name:
                malformed.append({"index": index, "reason": "named E does not match stack top; not guessed", "end_name": name, "top": compact(stack[-1])})
                unmatched_ends.append(item)
            else:
                start = stack.pop()
                if ts < start["ts"]:
                    malformed.append({"index": index, "reason": "negative B/E duration", "begin_index": start["index"]})
                else:
                    spans.append(dict(start, end=ts, end_index=index, end_args=args, pairing="B/E"))
        elif ph in ("b", "e", "S", "F"):
            id2 = e.get("id2", {})
            if isinstance(id2, dict) and "global" in id2:
                identity = ("global", str(id2["global"]))
            elif isinstance(id2, dict) and "local" in id2:
                identity = ("local", pid, str(id2["local"]))
            elif "id" in e:
                identity = ("legacy-process-local", pid, str(e["id"]))
            else:
                malformed.append({"index": index, "reason": "async event missing id"})
                continue
            key = (cat, str(e.get("scope", "")), identity, "nestable" if ph in ("b", "e") else "legacy")
            stack = async_stacks[key]
            if ph in ("b", "S"):
                stack.append(item)
            elif not stack:
                unmatched_ends.append(item)
            else:
                start = stack.pop()
                if ts < start["ts"]:
                    malformed.append({"index": index, "reason": "negative async duration", "begin_index": start["index"]})
                else:
                    spans.append(dict(start, end=ts, end_index=index, end_args=args, pairing="async"))
        elif ph in ("I", "i", "R", "C", "n", "T"):
            instants.append(item)
    pending = [s for stack in list(stacks.values()) + list(async_stacks.values()) for s in stack]
    window = None
    if alignment:
        if alignment.get("validated") is not True or not alignment.get("method") or not alignment.get("evidence"):
            raise ValueError("alignment must include validated=true plus method and evidence")
        a, b = alignment.get("trace_start_us"), alignment.get("trace_end_us")
        u = alignment.get("uncertainty_us")
        if not all(number(x) for x in (a, b, u)) or b <= a or u < 0:
            raise ValueError("invalid alignment interval/uncertainty")
        window = (a, b, u)
    def inside(ts):
        return window is None or window[0] <= ts <= window[1]
    def describe(item):
        return dict(item, args=compact(item.get("args", {})), **({"end_args": compact(item["end_args"])} if "end_args" in item else {}))
    buckets = collections.defaultdict(list)
    for s in spans:
        if INTEREST.search(s["name"]):
            buckets[(s["cat"], s["name"], s["pid"], s["tid"], s["pairing"])].append(s)
    summaries = []
    for (cat, name, pid, tid, pairing), rows in sorted(buckets.items()):
        chosen = [s for s in rows if inside(s["ts"])]
        closed = [s for s in chosen if window is None or s["end"] <= window[1]]
        times = sorted(s["ts"] for s in chosen)
        summaries.append({"cat": cat, "name": name, "pid": pid, "tid": tid,
                          "process_name": proc_names.get(pid), "thread_name": thread_names.get((pid, tid)),
                          "pairing": pairing, "whole_trace_count": len(rows),
                          "starts_in_window": len(chosen), "fully_in_window": len(closed),
                          "closed_duration_us": dist([s["end"] - s["ts"] for s in closed]),
                          "start_to_start_gap_us": dist([b-a for a,b in zip(times,times[1:])]),
                          "longest_closed": [describe(s) for s in sorted(closed, key=lambda s:s["end"]-s["ts"], reverse=True)[:3]]})
    selected_instants = [e for e in instants if INTEREST.search(e["name"])]
    instant_groups = collections.defaultdict(list)
    for e in selected_instants:
        if inside(e["ts"]):
            instant_groups[(e["name"], e["pid"], e["tid"])].append(e)
    instant_summary = []
    for (name,pid,tid), rows in sorted(instant_groups.items()):
        times=sorted(r["ts"] for r in rows)
        instant_summary.append({"name":name,"pid":pid,"tid":tid,"count":len(rows),
                                "start_to_start_gap_us":dist([b-a for a,b in zip(times,times[1:])]),
                                "first_args":compact(rows[0]["args"]),"last_args":compact(rows[-1]["args"])})
    drops = [e for e in selected_instants if e["name"] == "VideoFramesDropped"]
    role_rows = []
    for pid, tid in sorted(set(thread_times) | set(thread_names)):
        times = thread_times.get((pid, tid), [])
        role_rows.append({"pid":pid,"tid":tid,"process_name":proc_names.get(pid),"thread_name":thread_names.get((pid,tid)),
                          "first_ts":min(times) if times else None,"last_ts":max(times) if times else None,
                          "events":len(times),"backwards_timestamp_steps_in_export":sum(b<a for a,b in zip(times,times[1:]))})
    first, last = (min(timestamps), max(timestamps)) if timestamps else (None,None)
    boundaries = []
    if window:
        for s in spans:
            if INTEREST.search(s["name"]) and (s["ts"] < window[0] < s["end"] or s["ts"] < window[1] < s["end"]):
                boundaries.append(describe(s))
    if timeline is not None:
        timeline.update({"spans":[s for s in spans if INTEREST.search(s["name"])], "instants":selected_instants})
    md=root_metadata.get("metadata",{}) if isinstance(root_metadata,dict) else {}
    metadata_evidence={k:v for k,v in md.items() if k in ("clock-domain","command_line","trace-capture-datetime","trace_processor_stats","revision","user-agent","os-kernel-version")}
    return {"trace_metadata_evidence":metadata_evidence,"trace_event_count":len(events),"event_timestamp_bounds_us":[first,last],
            "root_metadata":compact(root_metadata,16384),"phase_counts":dict(phases),"categories":dict(categories),
            "alignment":alignment,"coverage":{"global_bounds_enclose_window_with_uncertainty":bool(window and first is not None and first <= window[0]-window[2] and last >= window[1]+window[2]),
                "complete_measured_window_proven":False,"reason":"Global bounds alone cannot rule out ring loss or a missing process/track. Check boundary spans, metadata, explicit data-loss signals and relevant per-track retention against external receipts."},
            "thread_roles":role_rows,"clock_markers":clocks[:100],"clock_markers_total":len(clocks),
            "explicit_loss_markers":lost_markers[:100],"explicit_loss_markers_total":len(lost_markers),
            "inventory":[{"category":c,"name":n,"phase":p,"pid":pid,"tid":tid,"count":v} for (c,n,p,pid,tid),v in sorted(counts.items())],
            "span_summaries":summaries,"instant_summaries":instant_summary,
            "drop_stat_updates":[dict(describe(e),in_window=inside(e["ts"])) for e in drops],
            "pairing":{"method":"Export order; strict per PID/TID B/E stack; async IDs and scope kept separate; flow events never treated as durations.",
                "unmatched_end_count":len(unmatched_ends),"unmatched_ends":[describe(e) for e in unmatched_ends[:200]],
                "pending_begin_count":len(pending),"pending_begins":[describe(e) for e in pending[:200]],
                "malformed_count":len(malformed),"malformed":malformed[:200],
                "closed_boundary_crossing_count":len(boundaries),"closed_boundary_crossings":boundaries[:200]},
            "limits":["Trace spans are elapsed wall time, never on-CPU time or kernel lock timing.",
                "Named-track media spans can be backdated decode-to-presentation intervals; do not interpret them as a thread holding a lock.",
                "VideoFramesDropped emits batched statistics updates, so a timestamp is not the precise drop instant.",
                "No decoder-starvation or presentation-cause verdict is inferred from event absence.",
                "Gap distributions use event start times, not time blocked waiting for the next event.",
                "Ring completeness and clock alignment require independent evidence; absent overflow markers do not prove no loss."]}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("trace")
    ap.add_argument('--trace-manifest',action='store_true',help='TRACE is an explicit complete event-shard manifest')
    ap.add_argument("--alignment")
    ap.add_argument("--timeline-output")
    ap.add_argument("--output",required=True)
    args=ap.parse_args()
    resource_limits()
    trace,identity=load_trace(args.trace,manifest=args.trace_manifest)
    alignment,align_identity=read_json(args.alignment,65536) if args.alignment else (None,None)
    trace_events(trace)
    if isinstance(trace,dict):
        events=trace.get("traceEvents")
        meta={k:v for k,v in trace.items() if k != "traceEvents"}
    else:
        events,meta=trace,{}
    timeline={} if args.timeline_output else None
    report=analyze(events,meta,alignment,timeline)
    if timeline is not None:
        write_json(args.timeline_output, timeline)
    report["input"]=identity
    report["alignment_input"]=align_identity
    report["parser_sha256"]=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    report["parser_inputs"] = parser_identity(__file__)
    write_json(args.output, report)
    print(json.dumps({"output":args.output,"trace_events":report["trace_event_count"],"input":identity,"pairing_counts":{k:v for k,v in report["pairing"].items() if k.endswith("count")}}))


if __name__ == "__main__":
    main()
