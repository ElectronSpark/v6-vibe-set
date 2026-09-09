# Desktop responsiveness audit — 2026-09-08 UTC

**Completed observational audit; no production fix.** The owned VM ran from
02:47:40 to 03:13:08 UTC and was synchronously reaped. Independent exact
inventory confirms zero QEMU processes.

Two problems reproduce: an isolated first move into a menu row can leave it
inactive until another small movement, and narrowing an existing Dolphin
window clips wrapped warning text. The menu behavior also occurs with direct
QMP input, bypassing Win32/SDL mouse delivery. The warning behavior depends on
the width at which the widget was created. These provide separate input/popup
and widget-layout boundaries to investigate; no kernel cause or fix is proven.

Repeated mouse sweeps, held marquee selection, window dragging and resizing,
Dolphin view changes, scrolling and overlapping-window exposure produce useful
content at reviewed endpoints. Rapid desktop sweeps leave transient older
hover decorations; tighter timing is recorded below. Successful settled
endpoints do not establish continuous motion smoothness or Linux parity.

This follows the [earlier mouse exploration](gui-mouse-audit-20260907.md) and
the [Chromium bottleneck investigation](chromium-bottleneck-investigation-20260908.md).
The latter localizes particular video-frame losses; it does not automatically
explain desktop hover or dragging behavior.

## Protected session and input route

Receipt: `build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/`.
The [interactive screenshot gallery][gallery] provides ordered burst frames
with their action, cursor and capture timestamps. The [interaction counts][metrics]
record **22 completed helper plans, 690 steps and 242 PNGs**: 461 explicit
move steps, 178 waits, 24 clicks, 10 button-down/up pairs, four wheel steps and
three right-click steps. There are also direct controller inputs. Counts show
executed host operations, not guest-consumed event counts or semantic passes.
The [provenance][provenance] and [actual QEMU arguments][cmdline] identify:

- Kernel `virgl-wait-progress-candidate-build-20260907T225130Z/xv6.bin`,
  SHA-256 `59253eb271555b6e1e3e035280002d79a7fc82f3e236b288ca981a0e495ed104`.
- Matching ELF SHA-256
  `ea72a11a3795509df404242bfd7014a75504bf6fedd1ef23ce970777c043ac82`.
- Protected base `audio-passcred-baseline-20260907T182610Z/fs.img`,
  8,053,063,680 bytes, retained modification time
  `1788805593827889142` ns. The run used a private qcow2 overlay, now removed.
  Base size, modification time and change time match before/after; this is
  metadata verification, not a newly calculated full-image hash.
- QEMU 9.0.2, KVM, six vCPUs, 8 GiB, corrected stock-APT SDL/OpenGL modules,
  `virtio-vga-gl` at 1280×800, and **`virtio-tablet-pci` with `vmport=off`**.
  This run's pointer route differs from the SDL vmmouse default.
- The actual append retains the 5,000-ms GPU stall watchdog, asynchronous
  presentation and the independent 60-Hz presentation clock. It also includes
  `kde_plasma_tooltip_delay=50`, `kde_tooltip_prewarm=1` and
  `kde_kickoff_prewarm=1`; this is not an untouched upstream desktop policy.
  The requested tooltip-delay setting is not evidence of a highlight-fade
  duration.

Preflight recorded zero QEMU processes at **02:47:36.991 UTC**. The controller
recorded owned QEMU PID 480131, start ticks 4329971, at **02:47:40.596 UTC**.
The first ready desktop capture completed at **02:48:11.425 UTC**.

Interaction uses real Win32 mouse delivery to the foreground-checked,
token-matched QEMU window. The retained burst helper calls `SetCursorPos` and
`mouse_event`; it records requested and observed client coordinates, button
state, action timestamps and capture timestamps. The captured client is
1356×897 at host position (188,62), containing the guest image and surrounding
window/inset pixels. Host cursor movement alone does not prove guest event
consumption or repaint. The helper's [syntax/plan validation][validation]
passes, including rejection of invalid actions, coordinates, duplicate capture
labels and delays.

## Infrastructure-only attempts

Two earlier boots are retained separately and provide no desktop burst
responsiveness result:

- `desktop-responsiveness-20260908T024226Z`: control-input EOF from the
  non-interactive controller invocation ended the run immediately after ready,
  before exploration. This was not a guest crash. Its cleanup records launcher
  exit 0, no remaining QEMU, overlay removal and unchanged base metadata.
- `desktop-responsiveness-20260908T024427Z`: the host burst helper failed at
  **02:45:26.584 UTC**. The controller records an infrastructure error, followed
  by launcher exit 0, no remaining QEMU, overlay removal and unchanged base
  metadata. This failed delivery path is not a guest responsiveness verdict.

## Reviewed visual observations

The [conductor's visual review][visual] records the actual visible outcomes,
separately from helper success or suggestive screenshot names.

| Trial | Retained observation | Scope |
| --- | --- | --- |
| Kickoff dismissal | Popup absent; desktop launchers and panel visible, without a stale popup rectangle. | Endpoint, not popup latency. |
| Dolphin launch | Home is mapped with folder items, Places sidebar, toolbar and status bar. | Mapping/content confirmed. |
| Fast desktop oscillation | Three bursts traverse five launcher positions with 16 moves and requested 25-ms spacing, ending over Dolphin. An immediate capture retains Konsole decoration and an earlier Terminal badge; a later quiet endpoint shows the correct Dolphin hover. | Transient mismatch; no claimed two-second stall. |
| Sampled hover repeat | Two 16-move sweeps use requested 30-ms spacing. Correct Dolphin-only hover is visible by 252.56/267.18 ms after final movement. | Host sampling bounds, described below. |
| Held desktop marquee | Expanded held rectangle encloses all five launchers, with selection fills. Contracting to a small empty held rectangle deselects them without remnants of the larger rectangle. | Correct held endpoints; release is not needed for these observed updates. |
| Held Dolphin title drag | Window moved from upper-left to a central/lower position. Its original footprint shows repainted wallpaper/icons and its moved content remains visible. | Correct quiet held endpoint; no continuous drag frame-rate claim. |
| Dolphin warning after resize | The first and a newly opened second window both clip the second warning line after narrowing. A warning created in a narrow split pane has enough height for both lines. | Repeated layout-history symptom, detailed below. Ordinary item-list viewport clipping is not classified as a defect. |
| Dolphin content controls | Icon-area marquee selects eight folders and the status bar confirms the count; clearing, wheel scrolling, compact/details views, tree expansion and split-pane activation/closing work. | Visible selection and content-state changes, not a timing benchmark. |
| Context menus | Activities and Create New submenus open after successive movements; isolated first-entry controls remain inactive until a small follow-up move. | Mixed behavior reproduced through host and QMP input; no blanket hover pass. |
| Overlapping windows | Narrowing and closing the second Dolphin window reveals correctly repainted content in the first. | No persistent stale footprint in these reviewed endpoints. |
| KWrite and window controls | Ordinary launch/Open dialog, existing guest `passwd` display, mouse drag selection through line 13, clearing, maximize/restore, minimize/taskbar restore and close work. Returning to the desktop leaves no stale window footprint. | No file edit/save; an earlier `/etc/services` attempt remained in the Open dialog and receives no successful-open credit. |

The completed burst summaries place fast oscillation at
**02:50:22.934–02:50:46.820 UTC**, sampled hover at
**02:51:09.218–02:51:32.273 UTC**, marquee trials at
**02:51:49.275–02:52:10.811 UTC**, and title dragging at
**02:53:27.963–02:53:37.009 UTC**. Their summary receipts report foreground
ownership, all requested steps completed and final button release. These
delivery checks supplement visual observations; they do not replace them.

## Persistent menu entry and warning clipping

**Menu entry:** `20-dolphin-context-hover/create-hover700ms.png` shows neither
a highlight nor a submenu at the first Create New target. Moving to Activities
opens its submenu, and moving back through several rows opens Create New.
The cleaner `22-context-initial-hover-quiet` repeat opens the popup and moves
once to client (900,501). The same row remains unhighlighted, with no submenu,
through the last quiet capture **9.242290 seconds** after that movement.
Only passive screenshots occur during this interval. A subsequent five-pixel
move to (905,501), still within the same row, is followed by a visible highlight
and submenu by the next quiet endpoint **2.129220 seconds** later. This is
input-associated recovery; the latter number is an observation bound, not an
exact opening latency. Host coordinates do not prove that the guest received
the first movement. Host/SDL delivery, guest input and popup entry semantics
remain distinct possibilities.

Trial 25's six-point host path at requested 20-ms spacing opens Create New
by its first quiet endpoint. A separate **QMP absolute-input control** then
reproduces the isolated-entry symptom: guest (862,442), mapped from client
(900,501) using the (38,59) inset, leaves the row inactive in captures 26 and
27. The command is logged at **03:06:04.877 UTC**, and the later inactive
capture completes at **03:06:48.084 UTC**, approximately 43 seconds later.
There is no intervening pointer command. A QMP move to (867,442) at
**03:07:10.734 UTC** is followed by the open submenu in capture 28 at
**03:07:13.578 UTC**. These controller/capture times are not precise guest
input-to-paint timestamps. The host cursor overlay stays at (770,399), which
is not evidence of the injected QMP pointer position. The controller sends
both absolute axes together, normalized against the 1280×800 guest display.
Reproduction through QMP shows that Win32/SDL mouse delivery is not required
for this symptom. It still leaves virtual input, guest input/popup handling
and painting as boundaries to distinguish.

**Warning clipping:** narrowing the first Dolphin window leaves the second
warning line clipped in `12-dolphin-corner-resize/released-2s.png`. Widening in
trial 13 restores one readable line. A warning newly created inside a split
pane starts with a taller banner and displays both complete lines at the same
approximately 654-pixel window width in trial 18, including its seven-second
quiet endpoint. Trial 18's intended widening missed the border and earns no
widening credit; a subsequent ordinary drag in trial 21 does widen the window.

An ordinary Ctrl+N creates a second wide Dolphin window with a fresh one-line
warning. Trial 24 narrows it, reproducing the clipped second line. It remains
clipped **7.146556 seconds after button release**, with no further input. The
underlying window exposed by narrowing repaints correctly. Thus this is a
repeated width/history-dependent warning-layout symptom, rather than evidence
of persistent damage across the whole compositor. The next source boundary
is warning height recalculation, described below.

## Hover timing and observer delay

The sampled-hover [step receipt][hoversteps] records both action completion
and the time at which `CopyFromScreen` finished copying client pixels.
Subtracting the last movement's `action_end_ms` gives:

| Repeat | Final movement step UTC | First pixels copied after movement | Correct-hover pixels copied after movement |
| --- | --- | ---: | ---: |
| 0 | 02:51:10.061593 | 20.504 ms; old decoration still visible | 252.557 ms |
| 1 | 02:51:18.652380 | 29.529 ms; immediate capture retained | 267.178 ms |

The reviewed clearing bound for repeat 0 lies between its two samples.
Repeat 1's reviewed endpoint establishes correct hover by 267.178 ms; no
additional visual verdict is inferred for its immediate capture.
The pointer remains at client (211,100), with no button held, throughout each
quiet sequence. The waits generate no new mouse movement.

PNG encoding/saving occurs synchronously between these samples. For example,
repeat 0's immediate pixel copy is at helper elapsed 864.3034 ms, while its
PNG finishes saving at 969.8626 ms, about 105.56 ms later. The next requested
100-ms wait therefore does not mean a sample exactly 100 ms after motion.
Use recorded pixel-copy timestamps rather than planned waits, filenames or
PNG-save completion. Captures are host observations with overhead, not guest
input-to-presentation timestamps.

The final helper buffers screenshots in memory and performs PNG encoding only
after all input/capture operations. A two-image smoke succeeds first. The
isolated control retains 12 images; two rapid sweeps retain 23 images under a
128-MiB pixel-storage cap. The new helper passes Windows PowerShell parsing
and validation checks for a valid plan, image count and memory cap. Its logs
write captured rows later, so analysis/gallery order them by step index.

| Buffered trial | Last reviewed incorrect capture window after final move | First fully correct capture window |
| --- | ---: | ---: |
| Isolated move to Dolphin | 152.380–173.991 ms: no hover | 198.623–214.834 ms |
| Rapid sweep 0 | 338.720–354.613 ms: Konsole border and Terminal badge | 369.068–389.947 ms |
| Rapid sweep 1 | 233.146–248.650 ms: Konsole border and multiple old badges | 264.487–288.624 ms |

The conductor visually inspected these transition endpoints. A read-only
[pixel comparison][pixelcomparison] also compares the entire five-icon row
against each final quiet reference; the first fully correct samples above
have identical pixels and stay identical in subsequent retained samples.
Earlier intermediate frames sometimes show the Dolphin border alongside
old badges, then another old highlight; those are not fully settled states.
The final sweeps therefore retain visible trails for hundreds of milliseconds,
with natural recovery observed by about **390 ms and 289 ms**. They differ
from the earlier 253/267-ms samples; neither set is a universal latency bound.
Screen copying, memory allocation and host scheduling still affect this
measurement. It does not establish native frame rate, physical display
latency, Linux parity or a proven normal animation duration.

The initial QMP screenshots report `no surface` while host captures succeed,
as in the earlier audit. This is a capture-path limitation, not evidence that
the visible desktop lacks a rendered surface. Host decoration, inset and any
occlusion remain visible in the PNGs; only unobscured guest pixels support
the visual conclusions.

## Bounded source interpretation

The warning-banner contrast identifies a separate layout boundary. The lock
selects Dolphin **23.08.5** and KWidgetsAddons **5.115.0**, without attesting
the running binaries or downstream patches. In the corresponding
[Dolphin source](https://github.com/KDE/dolphin/blob/v23.08.5/src/dolphinviewcontainer.cpp#L382-L414),
each view owns a warning widget, and showing a message chooses wrapping before
the animated show. In
[KMessageWidget](https://github.com/KDE/kwidgetsaddons/blob/v5.115.0/src/kmessagewidget.cpp#L266-L288),
Show/LayoutRequest recalculates a fixed height from `heightForWidth`, while the
resize path does so under a temporary animation flag. Compare the actual
height/minimum/maximum with `heightForWidth(currentWidth)` and trace
Resize/LayoutRequest delivery before attributing the clipping to GPU damage.
The observed dependence on widget creation history is consistent with this
boundary; it does not establish an upstream or kernel bug.

The repository package lock selects Plasma Desktop/Workspace **5.27.12** and
Plasma Framework **5.115.0**. This is evidence of intended locked inputs;
the running guest's package inventory and QML bytes have not yet been matched.

In upstream
[FolderItemDelegate.qml at v5.27.12](https://raw.githubusercontent.com/KDE/plasma-desktop/v5.27.12/containments/desktop/package/contents/ui/FolderItemDelegate.qml),
`hovered` follows the view's hovered item, hover/selection states directly
choose the frame prefix, and delegate/frame loaders are asynchronous.
The icon has `animated: false`. That file provides no explicit highlight-fade
transition duration. The matching
[FolderView.qml](https://raw.githubusercontent.com/KDE/plasma-desktop/v5.27.12/containments/desktop/package/contents/ui/FolderView.qml)
clears hover on relevant pointer exit/movement; its hover-activation timer
opens a folder popup rather than defining a highlight-fade deadline.
The bounded lookup did not establish the exact framework tooltip lifetime.
Thus these measurements cannot be labeled either a proven normal Plasma fade
or a particular missed-notification/rendering fault on that evidence alone.

The current input source gives each evdev open its own 256-record ring and
silently discards an oldest record if full. Its poll condition is that this
client's head and tail differ; file-level notification occurs outside the
device state lock. These facts motivate bounded bursts and examining the
actual consumer if delivery becomes suspicious. They do not show that the
observed bursts overflowed. Legacy `/dev/mouse` read counters do not measure
KWin's evdev consumption.

No observed result establishes missing softirq support, a defective lock,
an RCU stall or a scheduler root cause. Correct final pixels also do not prove
smooth intermediate motion or performance parity with Linux. No kernel or
desktop implementation is changed by this audit.

## Logs, limits and cleanup

The fresh `run.log` and `debugcon.log` retain virgl initialization, capsets,
1280×800 3D scanout, render-node setup and page-flip rebind messages. A bounded
explicit-file scan did not find the searched panic/quarantine messages; this
does not provide GPU-counter, IRQ, evdev-consumption or scheduling verdicts.
Inherited KDE log metadata is retained separately. No fresh compositor
renderer string is claimed from those append-only logs. No live debugger,
serial diagnostics, new tracing flags or kernel counters were used during
these interaction trials. Chromium, media/audio and matched Linux controls
were not repeated in this audit.

The [worker report][worker] records controller session 39011 exiting zero.
[Cleanup][cleanup] records launcher exit zero, no remaining QEMU, overlay
removal and unchanged base metadata. Worker and conductor independently ran
the exact inventory check; the [conductor receipt][inventory] reports both
QEMU counts zero. KWrite closed without an edit/save and the final reviewed
desktop has no stale window footprints. No builds or production source changes
were made. All submodules were clean at the final repository check.

**DESK-04 remains open.** Follow the first menu entry through virtual input,
evdev/libinput, popup enter/motion handling, paint/commit and presentation to
locate the missing transition. For warning clipping, compare widget geometry
and height-for-width recalculation first. Keep the transient hover trails as a
separate measured symptom, with a matched Linux/animation-policy control
needed before assigning cause.

[provenance]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/provenance.json
[cmdline]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/qemu-cmdline.json
[validation]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/burst-validation.json
[visual]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/root-visual-review.json
[hoversteps]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/07-desktop-sampled-hover/steps.jsonl
[gallery]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/index.html
[metrics]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/interaction-metrics.json
[pixelcomparison]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/buffered-hover-pixel-comparison.json
[worker]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/worker-report.json
[cleanup]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/cleanup.json
[inventory]: ../build-x86_64/gui-progress-audit/desktop-responsiveness-20260908T024736Z/root-final-exact-inventory.json
