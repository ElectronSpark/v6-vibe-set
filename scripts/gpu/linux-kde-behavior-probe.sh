#!/usr/bin/env bash
# Run inside the disposable Ubuntu/KDE reference guest, never on the host.
set -Eeuo pipefail

if [[ "$(hostname)" != "linux-kde-reference" ]]; then
    printf 'linux-kde-behavior: refusing host %s\n' "$(hostname)" >&2
    exit 2
fi

poweroff_on_finish=${LINUX_KDE_BEHAVIOR_POWEROFF:-1}
require_strace=${LINUX_KDE_BEHAVIOR_REQUIRE_STRACE:-1}
case "$poweroff_on_finish:$require_strace" in
    0:0|0:1|1:0|1:1) ;;
    *)
        echo "LINUX_KDE_BEHAVIOR_POWEROFF and LINUX_KDE_BEHAVIOR_REQUIRE_STRACE must be 0 or 1" >&2
        exit 2
        ;;
esac

fallback_out=/var/lib/linux-kde-reference
host_out=/mnt/linux-kde-reference-host
sudo install -d -m 0755 "$fallback_out"
out="$fallback_out"

finish()
{
    rc=$?
    set +e
    if ((rc == 0)); then
        printf 'LINUX_KDE_BEHAVIOR_DONE\n' | tee "$out/STATUS"
    else
        printf 'LINUX_KDE_BEHAVIOR_FAIL rc=%s\n' "$rc" | tee "$out/STATUS"
    fi
    sync
    sleep 2
    if [[ "$poweroff_on_finish" == 1 ]]; then
        sudo systemctl poweroff
    fi
    exit "$rc"
}
trap finish EXIT

sudo install -d -m 0755 "$host_out"
if mountpoint -q "$host_out"; then
    out="$host_out"
elif sudo mount -t 9p -o trans=virtio,version=9p2000.L,cache=mmap \
    hostshare "$host_out"; then
    out="$host_out"
fi

exec > >(tee -a "$out/behavior.log") 2>&1

printf 'behavior_started_utc=%s\n' "$(date -u +%FT%TZ)" | tee "$out/contract.txt"
cat >>"$out/contract.txt" <<'EOF'
reference_os=Ubuntu 24.04 noble
reference_desktop=KDE Plasma Wayland
qemu_cpus=6
qemu_memory=8G
display_target_physical=1280x768@60
accelerator=KVM
gpu=virtio-vga-gl virgl
host_display=sdl gl=on
audio=disabled
konsole_samples=1 first + 5 warm
idle_window_seconds=10
EOF

deadline=$((SECONDS + 120))
while :; do
    kwin_pid="$(pidof kwin_wayland 2>/dev/null | awk '{print $1}')"
    [[ -n "$kwin_pid" ]] || \
        kwin_pid="$(pidof kwin_wayland_wrapper 2>/dev/null | awk '{print $1}')"
    plasma_pid="$(pidof plasmashell 2>/dev/null | awk '{print $1}')"
    if [[ -n "$kwin_pid" && -n "$plasma_pid" ]]; then
        break
    fi
    if ((SECONDS >= deadline)); then
        printf 'desktop_ready=0 kwin_pid=%s plasma_pid=%s\n' \
            "${kwin_pid-}" "${plasma_pid-}" >"$out/desktop-ready.txt"
        exit 10
    fi
    sleep 1
done
printf 'desktop_ready=1 uptime_seconds=%s kwin_pid=%s plasma_pid=%s\n' \
    "$(cut -d' ' -f1 /proc/uptime)" "$kwin_pid" "$plasma_pid" \
    >"$out/desktop-ready.txt"

kscreen_deadline=$((SECONDS + 30))
while :; do
    kscreen_rc=0
    kscreen_raw="$(kscreen-doctor -o 2>&1)" || kscreen_rc=$?
    if [[ "$kscreen_raw" == *"Output:"* && "$kscreen_raw" == *"Modes:"* ]]; then
        break
    fi
    if ((SECONDS >= kscreen_deadline)); then
        printf 'display_target=1280x768@60 status=KSCREEN_NOT_READY\n%s\n' \
            "$kscreen_raw" >"$out/display-mode.txt"
        exit 11
    fi
    sleep 1
done
kscreen_state="$(printf '%s\n' "$kscreen_raw" | sed $'s/\x1B\\[[0-9;]*[A-Za-z]//g')"
output_id="$(printf '%s\n' "$kscreen_state" | awk '$1 == "Output:" {print $2; exit}')"
mode_id="$(printf '%s\n' "$kscreen_state" | tr ' ' '\n' | \
    sed -n 's/^\([0-9][0-9]*\):1280x768@60[*!]*$/\1/p;T;q')"
if [[ -z "$output_id" || -z "$mode_id" ]]; then
    printf 'display_target=1280x768@60 output=%s mode=%s status=NOT_FOUND\n%s\n' \
        "${output_id-}" "${mode_id-}" "$kscreen_state" >"$out/display-mode.txt"
    exit 12
fi
kscreen-doctor "output.$output_id.mode.$mode_id" \
    >"$out/display-mode-set.txt" 2>&1 || true
sleep 12
kscreen_rc=0
kscreen_raw="$(kscreen-doctor -o 2>&1)" || kscreen_rc=$?
kscreen_state="$(printf '%s\n' "$kscreen_raw" | sed $'s/\x1B\\[[0-9;]*[A-Za-z]//g')"
if [[ "$kscreen_state" != *"1280x768@60*"* ]]; then
    printf 'display_target=1280x768@60 verify_rc=%s status=FAIL\n%s\n' \
        "$kscreen_rc" "$kscreen_state" >"$out/display-mode.txt"
    exit 13
fi
printf 'display_target=1280x768@60 verify_rc=%s status=PASS\n%s\n' \
    "$kscreen_rc" "$kscreen_state" >"$out/display-mode.txt"
printf 'display_actual_physical=1280x768@60\n' >>"$out/contract.txt"

{
    printf 'uname='; uname -a
    printf 'os_release='; tr '\n' ' ' </etc/os-release; printf '\n'
    printf 'plasma_version='; plasmashell --version 2>&1 || true
    printf 'kwin_version='; kwin_wayland --version 2>&1 || true
    printf 'konsole_version='; konsole --version 2>&1 || true
    printf 'session_type=%s\n' "${XDG_SESSION_TYPE-}"
    printf 'online_cpus='; getconf _NPROCESSORS_ONLN
    lscpu
    free -b
    printf 'perf_event_paranoid='; cat /proc/sys/kernel/perf_event_paranoid
    printf 'timer_list_hz='; getconf CLK_TCK
} >"$out/system.txt"

{
    printf '%s\n' '=== glxinfo -B ==='
    glxinfo -B 2>&1 || true
    printf '%s\n' '=== eglinfo -B ==='
    eglinfo -B 2>&1 || true
} >"$out/renderer.txt"
renderer_text="$(cat "$out/renderer.txt")"
if [[ "$renderer_text" != *"virgl (D3D12 (NVIDIA"* ]]; then
    printf 'renderer_status=FAIL expected=virgl-nvidia\n' >"$out/renderer-status.txt"
    exit 14
fi
printf 'renderer_status=PASS active=virgl-nvidia\n' >"$out/renderer-status.txt"

{
    printf 'xdg_session_type=%s\n' "${XDG_SESSION_TYPE-}"
    printf 'wayland_display=%s\n' "${WAYLAND_DISPLAY-}"
    printf 'qt_qpa_platform=%s\n' "${QT_QPA_PLATFORM-}"
    printf 'runtime_dir=%s\n' "${XDG_RUNTIME_DIR-}"
    test -S "${XDG_RUNTIME_DIR-}/${WAYLAND_DISPLAY-}"
    printf 'wayland_socket_status=PASS\n'
} >"$out/wayland-session.txt"
if command -v qdbus >/dev/null 2>&1 && \
    timeout 10s qdbus org.kde.KWin /KWin supportInformation \
        >"$out/kwin-dbus-roundtrip.txt" 2>&1; then
    printf 'kwin_dbus_roundtrip=PASS\n' >"$out/kwin-dbus-status.txt"
else
    printf 'kwin_dbus_roundtrip=UNAVAILABLE\n' >"$out/kwin-dbus-status.txt"
fi
if command -v wayland-info >/dev/null 2>&1; then
    if timeout 10s wayland-info >"$out/wayland-info.txt" 2>&1; then
        printf 'wayland_registry_roundtrip=PASS\n' >"$out/wayland-info-status.txt"
    else
        printf 'wayland_registry_roundtrip=FAIL\n' >"$out/wayland-info-status.txt"
        exit 21
    fi
else
    printf 'wayland_registry_roundtrip=UNAVAILABLE tool=wayland-info\n' \
        >"$out/wayland-info-status.txt"
fi
sudo ps -L -p "$kwin_pid" -o pid,tid,psr,stat,wchan:32,pcpu,comm \
    >"$out/kwin-thread-waits.txt"

snapshot_system()
{
    label=$1
    dest="$out/system-$label.txt"
    {
        printf '%s\n' '=== uptime ==='; cat /proc/uptime
        printf '%s\n' '=== stat ==='; cat /proc/stat
        printf '%s\n' '=== vmstat ==='; cat /proc/vmstat
        printf '%s\n' '=== meminfo ==='; cat /proc/meminfo
        printf '%s\n' '=== pressure cpu ==='; cat /proc/pressure/cpu
        printf '%s\n' '=== pressure memory ==='; cat /proc/pressure/memory
        printf '%s\n' '=== pressure io ==='; cat /proc/pressure/io
        printf '%s\n' '=== schedstat ==='; cat /proc/schedstat
        printf '%s\n' '=== diskstats ==='; cat /proc/diskstats
        printf '%s\n' '=== processes ==='
        ps -eLo pid,ppid,tid,psr,pri,ni,stat,pcpu,pmem,rss,etimes,comm,args \
            --sort=-pcpu
    } >"$dest"
}

read_cpu()
{
    awk '/^cpu / {total=0; for (i=2; i<=NF; i++) total+=$i; print total,$5+$6; exit}' \
        /proc/stat
}

read_stat_key()
{
    awk -v key="$1" '$1 == key {print $2; exit}' /proc/stat
}

read_vm_key()
{
    awk -v key="$1" '$1 == key {print $2; exit}' /proc/vmstat
}

read_proc_ticks()
{
    awk '{print $14+$15}' "/proc/$1/stat"
}

capture_process_chain()
{
    sample=$1
    pid=$2
    dest="$out/konsole-$sample-process-chain.txt"
    : >"$dest"
    depth=0
    while [[ "$pid" =~ ^[0-9]+$ && "$pid" -gt 1 && -r "/proc/$pid/status" && \
        "$depth" -lt 8 ]]; do
        {
            printf '=== depth=%s pid=%s ===\n' "$depth" "$pid"
            cat "/proc/$pid/status"
            printf '%s\n' '--- sched ---'
            cat "/proc/$pid/sched" 2>/dev/null || true
            printf '%s\n' '--- schedstat ---'
            cat "/proc/$pid/schedstat" 2>/dev/null || true
            printf '%s\n' '--- io ---'
            cat "/proc/$pid/io" 2>/dev/null || true
            printf '%s\n' '--- wchan ---'
            cat "/proc/$pid/wchan" 2>/dev/null || true
        } >>"$dest"
        pid="$(awk '/^PPid:/ {print $2; exit}' "/proc/$pid/status")"
        depth=$((depth + 1))
    done
}

snapshot_system before
read -r idle_total0 idle_idle0 < <(read_cpu)
kwin_idle0="$(read_proc_ticks "$kwin_pid")"
plasma_idle0="$(read_proc_ticks "$plasma_pid")"
vmstat 1 10 >"$out/idle-vmstat.txt"
read -r idle_total1 idle_idle1 < <(read_cpu)
kwin_idle1="$(read_proc_ticks "$kwin_pid")"
plasma_idle1="$(read_proc_ticks "$plasma_pid")"
hz="$(getconf CLK_TCK)"
awk -v t0="$idle_total0" -v t1="$idle_total1" \
    -v i0="$idle_idle0" -v i1="$idle_idle1" \
    -v k0="$kwin_idle0" -v k1="$kwin_idle1" \
    -v p0="$plasma_idle0" -v p1="$plasma_idle1" -v hz="$hz" \
    'BEGIN {dt=t1-t0; di=i1-i0;
      printf "idle_seconds=10\nguest_cpu_busy_percent=%.3f\n",100*(dt-di)/dt;
      printf "kwin_cpu_percent_one_core=%.3f\n",100*(k1-k0)/(hz*10);
      printf "plasmashell_cpu_percent_one_core=%.3f\n",100*(p1-p0)/(hz*10)}' \
    >"$out/idle-summary.txt"

ctxt0="$(read_stat_key ctxt)"
fork0="$(read_stat_key processes)"
fault0="$(read_vm_key pgfault)"
majfault0="$(read_vm_key pgmajfault)"
printf 'sample\tclass\tstart_ns\tready_ns\twait_ms\tshell_pid\tstatus\n' \
    >"$out/konsole-launch.tsv"
for sample in 1 2 3 4 5 6; do
    marker="/run/user/$(id -u)/linux-kde-behavior-ready-$sample"
    rm -f "$marker"
    start_ns="$(date +%s%N)"
    konsole --separate -e /bin/bash -lc \
        "printf '%s %s\\n' \"\$\$\" \"\$(date +%s%N)\" > '$marker'; sleep 3" \
        >"$out/konsole-$sample.stdout" 2>"$out/konsole-$sample.stderr" &
    launcher_pid=$!
    sample_deadline=$((SECONDS + 20))
    while [[ ! -s "$marker" && $SECONDS -lt $sample_deadline ]]; do sleep 0.01; done
    if [[ -s "$marker" ]]; then
        read -r shell_pid ready_ns <"$marker"
        wait_ms=$(((ready_ns - start_ns) / 1000000))
        status=PASS
        if [[ "$sample" == 1 || "$sample" == 2 ]]; then
            capture_process_chain "$sample" "$shell_pid"
        fi
    else
        shell_pid=0
        ready_ns=0
        wait_ms=-1
        status=TIMEOUT
    fi
    class=warm; [[ "$sample" == 1 ]] && class=first
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$sample" "$class" \
        "$start_ns" "$ready_ns" "$wait_ms" "$shell_pid" "$status" \
        >>"$out/konsole-launch.tsv"
    wait "$launcher_pid" 2>/dev/null || true
    rm -f "$marker"
    sleep 1
done
ctxt1="$(read_stat_key ctxt)"
fork1="$(read_stat_key processes)"
fault1="$(read_vm_key pgfault)"
majfault1="$(read_vm_key pgmajfault)"

awk -F '\t' 'NR > 1 && $2 == "warm" && $7 == "PASS" {print $5}' \
    "$out/konsole-launch.tsv" | sort -n >"$out/konsole-warm-ms.sorted"
awk 'BEGIN {n=0} {a[++n]=$1} END {
  if (!n) exit 1; sum=0; for (i=1;i<=n;i++) sum+=a[i];
  med=(n%2)?a[(n+1)/2]:(a[n/2]+a[n/2+1])/2;
  printf "warm_samples=%d\nwarm_min_ms=%d\nwarm_median_ms=%.1f\n",n,a[1],med;
  printf "warm_max_ms=%d\nwarm_mean_ms=%.1f\n",a[n],sum/n}' \
    "$out/konsole-warm-ms.sorted" >"$out/konsole-summary.txt"
{
    printf 'system_context_switches_delta=%s\n' "$((ctxt1 - ctxt0))"
    printf 'system_processes_created_delta=%s\n' "$((fork1 - fork0))"
    printf 'system_minor_plus_major_faults_delta=%s\n' "$((fault1 - fault0))"
    printf 'system_major_faults_delta=%s\n' "$((majfault1 - majfault0))"
} >"$out/konsole-system-deltas.txt"

instrument_marker="/run/user/$(id -u)/linux-kde-instrument-ready"
rm -f "$instrument_marker"
timeout 20s env LD_DEBUG=statistics konsole --separate -e /bin/bash -lc \
    "date +%s%N > '$instrument_marker'; sleep 2" \
    >"$out/konsole-lddebug.stdout" 2>"$out/konsole-lddebug.txt" || true

strace_bin=/opt/linux-kde-behavior/strace
strace_lib=/opt/linux-kde-behavior/lib
if [[ ! -x "$strace_bin" ]]; then
    printf 'strace_status=MISSING\n' >"$out/strace-status.txt"
    if [[ "$require_strace" == 1 ]]; then
        exit 15
    fi
else
    rm -f "$instrument_marker"
    timeout 30s env LD_LIBRARY_PATH="$strace_lib" "$strace_bin" -f -qq -c \
        -o "$out/konsole-strace-summary.txt" \
        env -u LD_LIBRARY_PATH konsole --separate -e /bin/bash -lc \
        "date +%s%N > '$instrument_marker'; sleep 2" || true
    if [[ ! -s "$out/konsole-strace-summary.txt" ]]; then
        printf 'strace_status=EMPTY\n' >"$out/strace-status.txt"
        exit 16
    fi
    rm -f "$instrument_marker"
    timeout 30s env LD_LIBRARY_PATH="$strace_lib" "$strace_bin" -f -qq \
        -e trace=%file -s 160 -o "$out/konsole-strace-file.txt" \
        env -u LD_LIBRARY_PATH konsole --separate -e /bin/bash -lc \
        "date +%s%N > '$instrument_marker'; sleep 2" || true
    printf 'strace_status=PASS\n' >"$out/strace-status.txt"
fi

glmark_bin="$(command -v glmark2-wayland || command -v glmark2-es2-wayland || \
    command -v glmark2 || true)"
if [[ -z "$glmark_bin" ]]; then
    printf 'glmark_status=MISSING\n' >"$out/glmark-load-summary.txt"
    exit 17
fi
read -r load_total0 load_idle0 < <(read_cpu)
kwin_load0="$(read_proc_ticks "$kwin_pid")"
plasma_load0="$(read_proc_ticks "$plasma_pid")"
load_start_ns="$(date +%s%N)"
vmstat 1 15 >"$out/glmark-vmstat.txt" & vmstat_pid=$!
timeout --kill-after=10s 180s "$glmark_bin" --size 800x600 \
    --benchmark build --benchmark texture --benchmark shading \
    --benchmark buffer --benchmark ideas >"$out/glmark2.txt" 2>&1 || true
wait "$vmstat_pid" 2>/dev/null || true
load_end_ns="$(date +%s%N)"
read -r load_total1 load_idle1 < <(read_cpu)
kwin_load1="$(read_proc_ticks "$kwin_pid")"
plasma_load1="$(read_proc_ticks "$plasma_pid")"
awk -v t0="$load_total0" -v t1="$load_total1" \
    -v i0="$load_idle0" -v i1="$load_idle1" \
    -v k0="$kwin_load0" -v k1="$kwin_load1" \
    -v p0="$plasma_load0" -v p1="$plasma_load1" -v hz="$hz" \
    -v ns0="$load_start_ns" -v ns1="$load_end_ns" \
    'BEGIN {dt=t1-t0; di=i1-i0; sec=(ns1-ns0)/1e9;
      printf "load_seconds=%.3f\nguest_cpu_busy_percent=%.3f\n",sec,100*(dt-di)/dt;
      printf "kwin_cpu_percent_one_core=%.3f\n",100*(k1-k0)/(hz*sec);
      printf "plasmashell_cpu_percent_one_core=%.3f\n",100*(p1-p0)/(hz*sec)}' \
    >"$out/glmark-load-summary.txt"
glmark_text="$(cat "$out/glmark2.txt")"
if [[ "$glmark_text" != *"GL_RENDERER:    virgl (D3D12 (NVIDIA"* || \
    "$glmark_text" != *"glmark2 Score:"* ]]; then
    exit 18
fi

snapshot_system after
konsole_pids="$(pidof konsole 2>/dev/null || true)"
if [[ -n "$konsole_pids" ]]; then
    konsole_processes_after="$(wc -w <<<"$konsole_pids")"
else
    konsole_processes_after=0
fi
{
    printf 'konsole_processes_after=%s\n' "$konsole_processes_after"
    printf 'zombie_processes_after='; ps -e -o stat= | awk '$1 ~ /^Z/ {n++} END {print n+0}'
} >"$out/teardown-summary.txt"

if command -v spectacle >/dev/null; then
    timeout 30s spectacle -b -n -f -o "$out/plasma-behavior.png" \
        >"$out/spectacle.log" 2>&1 || true
fi
if [[ ! -f "$out/plasma-behavior.png" ]]; then
    exit 19
fi
read -r screenshot_width screenshot_height < <(
    python3 -c 'import struct,sys; d=open(sys.argv[1],"rb").read(24); print(*struct.unpack(">II",d[16:24]))' \
        "$out/plasma-behavior.png"
)
printf 'screenshot_width=%s\nscreenshot_height=%s\n' \
    "$screenshot_width" "$screenshot_height" >"$out/screenshot-size.txt"
if [[ "$screenshot_width" != 1280 || "$screenshot_height" != 768 ]]; then
    exit 20
fi

dmesg >"$out/dmesg.txt" 2>&1 || true
journalctl -b --no-pager >"$out/journal.txt" 2>&1 || true
printf 'behavior_finished_utc=%s\n' "$(date -u +%FT%TZ)" >>"$out/contract.txt"
