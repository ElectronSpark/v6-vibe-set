#!/usr/bin/env python3
"""Exercise the actual GPU progress-wait functions with deterministic host stubs.

This tests the waiter-registration contract, not a GPU device or scheduler.
The old two-argument and new three-argument APIs are accepted for before/after
receipts; both must satisfy the same behavioral assertions. No source rewriting
or replacement wait logic is used. All generated files stay in --output-dir.
"""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "kernel/kernel/virtio_gpu.c"
FUNCTIONS = (
    "virtio_gpu_wait_cond_ready",
    "virtio_gpu_wait_for_used",
    "virtio_gpu_async_wait_progress",
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def brace_end(text, start):
    """Find a C definition's end, ignoring comments and quoted literals."""
    depth = 0
    state = "code"
    index = start
    while index < len(text):
        char = text[index]
        following = text[index:index + 2]
        if state in ("string", "char"):
            if char == "\\":
                index += 2
                continue
            if char == ('"' if state == "string" else "'"):
                state = "code"
        elif state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if following == "*/":
                state = "code"
                index += 2
                continue
        elif following in ("//", "/*"):
            state = "line" if following == "//" else "comment"
            index += 2
            continue
        elif char in ('"', "'"):
            state = "string" if char == '"' else "char"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise ValueError("unterminated C definition")


def extract(source):
    # Locate declarations only through the repository's guarded source search.
    pattern = (r"^(static int (virtio_gpu_wait_cond_ready|"
               r"virtio_gpu_wait_for_used|virtio_gpu_async_wait_progress)\(|"
               r"struct virtio_gpu_wait_cond \{)")
    command = ["/home/es/.local/bin/rg", "-n", pattern,
               str(SOURCE.relative_to(ROOT))]
    found = subprocess.run(command, cwd=ROOT, check=True, capture_output=True,
                           text=True, timeout=15)
    lines = source.splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    fragments = {}
    locations = {}
    for result in found.stdout.splitlines():
        line_number, declaration = result.split(":", 1)
        start = offsets[int(line_number) - 1]
        opening = source.index("{", start)
        semicolon = source.find(";", start, opening)
        if semicolon >= 0:
            continue  # Forward declaration, not a definition.
        end = brace_end(source, opening)
        if declaration.startswith("struct "):
            name = "virtio_gpu_wait_cond"
            if source[end] != ";":
                raise ValueError("unexpected wait-condition struct suffix")
            end += 1
        else:
            name = declaration.split("(", 1)[0].split()[-1]
        if name in fragments:
            raise ValueError(f"duplicate definition: {name}")
        fragments[name] = source[start:end]
        locations[name] = int(line_number)
    expected = {"virtio_gpu_wait_cond", *FUNCTIONS}
    if set(fragments) != expected:
        raise ValueError(f"unexpected definitions: {sorted(fragments)}")
    declaration = fragments["virtio_gpu_async_wait_progress"].split("{", 1)[0]
    arguments = declaration[declaration.index("(") + 1:declaration.rindex(")")]
    argument_count = len(arguments.split(","))
    if argument_count not in (2, 3):
        raise ValueError(f"unsupported wait API: {argument_count} arguments")
    return fragments, locations, argument_count, command


PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef struct { int locked; } mutex_t;
typedef struct { unsigned int done; } completion_t;
struct used_ring { uint16 idx; };
struct virtio_gpu_queue {
    int lock;
    struct used_ring *used;
    uint16 used_idx;
    unsigned int sync_done;
    unsigned int sync_stale;
};
struct virtio_gpu {
    struct virtio_gpu_queue ctrlq;
    uint64 async_retire_seq;
    unsigned int async_count;
    unsigned int async_abandoned;
    mutex_t async_wait_serialize;
    completion_t async_wait;
};
#define VIRTIO_GPU_IRQ_WAIT_MS 5000
#define VIRTIO_GPU_IRQ_WAIT_MS_MAX 60000
#define VIRTIO_GPU_IRQ_WAIT_SLICE_MS 1
#define VIRTIO_GPU_FAST_POLL_LIMIT 4
#define VIRTIO_GPU_POLL_LIMIT 4

enum injection { BEFORE_ENTRY, DURING_SERIALIZE, AFTER_ARM_SIGNAL,
                 AFTER_ARM_SILENT, UNCHANGED, USED_OCCUPIED };
enum pending_kind { ACTIVE, ABANDONED, STALE_SYNC };
static struct virtio_gpu gpu;
static struct used_ring used;
static enum injection injection;
static unsigned int sleep_calls, arm_calls, retirement_calls, predicate_checks;
static int caller_pending;

/* A completion event supplied by the harness, not a replacement wait path. */
static void retire(void)
{
    assert(!gpu.ctrlq.lock);
    ++retirement_calls;
    ++gpu.async_retire_seq;
    gpu.async_count = gpu.async_abandoned = gpu.ctrlq.sync_stale = 0;
    caller_pending = 0;
    gpu.async_wait.done = 1;
}
static void mutex_lock(mutex_t *lock)
{
    assert(!lock->locked);
    lock->locked = 1;
    if (injection == DURING_SERIALIZE && !retirement_calls)
        retire();
}
static void mutex_unlock(mutex_t *lock)
{
    assert(lock->locked);
    lock->locked = 0;
}
static int spin_lock_irqsave(int *lock)
{
    assert(!*lock);
    *lock = 1;
    return 0;
}
static void spin_unlock_irqrestore(int *lock, int saved)
{
    (void)saved;
    assert(*lock);
    *lock = 0;
}
static void completion_reinit(completion_t *completion)
{
    assert(gpu.ctrlq.lock);
    ++arm_calls;
    completion->done = 0;
}
static int wait_for_completion_timeout(completion_t *completion, uint32 ms)
{
    assert(!gpu.ctrlq.lock && gpu.async_wait_serialize.locked && ms == 1);
    ++sleep_calls;
    if ((injection == AFTER_ARM_SIGNAL || injection == AFTER_ARM_SILENT) &&
        !retirement_calls) {
        retire();
        if (injection == AFTER_ARM_SILENT)
            completion->done = 0; /* Exercise actual condition polling. */
    }
    return completion->done != 0;
}
static int virtio_gpu_submit_trace_enabled(void) { return 0; }
static uint64 r_time(void) { return sleep_calls; }
static uint32 virtio_gpu_cmdline_uint(const char *name, uint32 value, uint32 max)
{ (void)name; (void)max; return value; }
static void virtio_gpu_count_poll_fallback(struct virtio_gpu *g) { (void)g; }
static void virtio_gpu_submit_trace_record_wait_for_used(struct virtio_gpu *g,
                                                        uint64 ticks)
{ (void)g; (void)ticks; }
static void virtio_gpu_submit_trace_record_async_wait_progress(struct virtio_gpu *g,
                                                              uint64 ticks)
{ (void)g; (void)ticks; }
'''


TESTS = r'''
struct test_case {
    const char *name;
    enum injection injection;
    enum pending_kind kind;
};
static int caller_predicate(void)
{
    ++predicate_checks;
    return caller_pending;
}
static int run_case(const struct test_case *test)
{
    uint64 baseline;
    int result, still_pending, ok, expected_result;
    unsigned int expected_sleeps, expected_arms, expected_retirements;
    memset(&gpu, 0, sizeof(gpu));
    used.idx = 100;
    gpu.ctrlq.used = &used;
    gpu.ctrlq.used_idx = 100;
    gpu.async_retire_seq = 40;
    if (test->kind == ACTIVE) gpu.async_count = 1;
    if (test->kind == ABANDONED) gpu.async_abandoned = 1;
    if (test->kind == STALE_SYNC) gpu.ctrlq.sync_stale = 1;
    injection = test->injection;
    sleep_calls = arm_calls = retirement_calls = predicate_checks = 0;
    caller_pending = 1;

    /* Registration baseline must precede the caller's predicate check. */
    baseline = __atomic_load_n(&gpu.async_retire_seq, __ATOMIC_ACQUIRE);
    assert(caller_predicate());
    if (injection == BEFORE_ENTRY) retire();
    if (injection == USED_OCCUPIED) ++used.idx;

#if WAIT_API_ARGUMENTS == 3
    result = virtio_gpu_async_wait_progress(&gpu, 3, baseline);
#else
    (void)baseline;
    result = virtio_gpu_async_wait_progress(&gpu, 3);
#endif
    still_pending = caller_predicate();

    expected_result = injection != UNCHANGED;
    expected_sleeps = injection == UNCHANGED ? 3 :
        (injection == AFTER_ARM_SIGNAL || injection == AFTER_ARM_SILENT ? 1 : 0);
    expected_arms = injection == UNCHANGED || injection == AFTER_ARM_SIGNAL ||
        injection == AFTER_ARM_SILENT;
    expected_retirements = injection != UNCHANGED && injection != USED_OCCUPIED;
    ok = result == expected_result && sleep_calls == expected_sleeps &&
        arm_calls == expected_arms && retirement_calls == expected_retirements &&
        predicate_checks == 2 &&
        still_pending == (injection == UNCHANGED || injection == USED_OCCUPIED) &&
        !gpu.ctrlq.lock && !gpu.async_wait_serialize.locked &&
        gpu.async_retire_seq == baseline + expected_retirements;
    printf("{\"case\":\"%s\",\"status\":\"%s\",\"result\":%d,"
           "\"expected_result\":%d,\"sleep_calls\":%u,\"expected_sleep_calls\":%u,"
           "\"arm_calls\":%u,\"retirement_calls\":%u,\"predicate_checks\":%u,"
           "\"still_pending\":%d,\"baseline\":%llu,\"final_sequence\":%llu}\n",
           test->name, ok ? "PASS" : "FAIL", result, expected_result,
           sleep_calls, expected_sleeps, arm_calls, retirement_calls,
           predicate_checks, still_pending, (unsigned long long)baseline,
           (unsigned long long)gpu.async_retire_seq);
    return !ok;
}
int main(void)
{
    static const struct test_case cases[] = {
        {"retire-before-entry-active", BEFORE_ENTRY, ACTIVE},
        {"retire-before-entry-abandoned", BEFORE_ENTRY, ABANDONED},
        {"retire-before-entry-stale-sync", BEFORE_ENTRY, STALE_SYNC},
        {"retire-during-serialize", DURING_SERIALIZE, ACTIVE},
        {"pending-active-no-progress", UNCHANGED, ACTIVE},
        {"pending-abandoned-no-progress", UNCHANGED, ABANDONED},
        {"pending-stale-sync-no-progress", UNCHANGED, STALE_SYNC},
        {"retire-after-arm-signaled", AFTER_ARM_SIGNAL, ACTIVE},
        {"retire-after-arm-polled", AFTER_ARM_SILENT, ACTIVE},
        {"unconsumed-used-entry", USED_OCCUPIED, ACTIVE},
    };
    int failures = 0;
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        failures += run_case(&cases[i]);
    return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path,
                        help="new directory retaining source fragments, binary, and results")
    parser.add_argument("--cc", default="cc", help="host C compiler executable")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source_bytes = SOURCE.read_bytes()
    source = source_bytes.decode()
    fragments, locations, argument_count, search_command = extract(source)
    order = ("virtio_gpu_wait_cond", *FUNCTIONS)
    extracted = "\n\n".join(fragments[name] for name in order) + "\n"
    harness = (PREAMBLE + f"\n#define WAIT_API_ARGUMENTS {argument_count}\n" +
               extracted + TESTS)
    (output / "production-fragments.c").write_text(extracted)
    (output / "wait-progress-test.c").write_text(harness)
    compile_command = [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-parameter", str(output / "wait-progress-test.c"),
                       "-o", str(output / "wait-progress-test")]
    receipt = {
        "scope": "host unit: exact production waiter bodies; deterministic platform stubs; no GPU",
        "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source": str(SOURCE), "source_sha256": digest(source_bytes),
        "script_sha256": digest(Path(__file__).read_bytes()),
        "wait_api_arguments": argument_count, "source_lines": locations,
        "function_sha256": {name: digest(fragments[name].encode()) for name in order},
        "fragments_sha256": digest(extracted.encode()),
        "harness_sha256": digest(harness.encode()),
        "source_search_command": search_command,
        "compile_command": compile_command,
    }
    (output / "commands.txt").write_text(shlex.join(compile_command) + "\n")
    compiled = subprocess.run(compile_command, capture_output=True, text=True, timeout=30)
    (output / "compile.stdout").write_text(compiled.stdout)
    (output / "compile.stderr").write_text(compiled.stderr)
    receipt["compile_exit"] = compiled.returncode
    if compiled.returncode:
        receipt["status"] = "COMPILE_ERROR"
        (output / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(compiled.stderr, file=sys.stderr)
        return 2
    binary = output / "wait-progress-test"
    receipt["binary_sha256"] = digest(binary.read_bytes())
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    (output / "run.stdout").write_text(run.stdout)
    (output / "run.stderr").write_text(run.stderr)
    receipt["run_exit"] = run.returncode
    receipt["cases"] = [json.loads(line) for line in run.stdout.splitlines()]
    receipt["status"] = "PASS" if run.returncode == 0 and len(receipt["cases"]) == 10 else "FAIL"
    (output / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(run.stdout, end="")
    print(f"RESULT {receipt['status']} api_arguments={argument_count} receipt={output / 'results.json'}")
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
