# Mouse exploration audit — 2026-09-07

**74 mouse actions exercised desktop icons, menus, buttons, folder views,
scrolling, dragging, application settings and calendar views.** Dolphin,
KWrite and the tested KDE controls remained usable. Chromium's New Tab `+`
button caused its window to disappear twice: once with horizontal tabs and
once with vertical tabs. The first relaunch displayed “Chromium didn't shut
down correctly.” This is the main new functional failure.

[Browse all 70 screenshots in the gallery][gallery], with the corresponding
mouse action, timestamp and observation. This follows the
[earlier general GUI audit](gui-progress-audit-20260907.md); follow-up work stays
in the [single active plan](active-work-plan.md).

## Session and method

- **UTC:** 2026-09-07 15:44:32–16:03:53, approximately 19 minutes.
- **Token:** `mouse-audit-20260907T154432Z`.
- Existing image, no rebuild: the same kernel SHA-256
  `af174a9fc9919fd9990942aff8e2b14a406a663ef6cd47d5b3a8320bb0472b21`
  and base filesystem as the earlier audit. KVM, six vCPUs, 8 GiB, corrected
  stock-APT SDL/OpenGL modules, `virtio-vga-gl`, 1280x800 guest mode.
  [Provenance][provenance] and [actual QEMU arguments][cmdline] are retained.
- All application interaction used the host mouse in the foreground-checked,
  token-matched QEMU window: **53 single clicks, 10 moves/hovers, five double
  clicks, two right clicks, two wheel actions and two drags**. No keyboard
  events or guest serial commands were used. The [action log][actions] records
  all 74 actions and successful host-input helper results.
- Changes to sorting, editor gutter and browser appearance applied only to
  the temporary session. No repository runtime implementation was changed.

## Observed behavior

| Area | Mouse actions and visible result | Captures |
| --- | --- | --- |
| Desktop icons | Single clicks selected Dolphin/Chromium; double clicks launched them. Dolphin icon hover showed its selection affordance. | [2][f02], [6][f06], [7][f07], [42][f42], [43][f43] |
| Desktop context menu | Right click opened the menu; clicking Icons opened its nested options; clicking outside dismissed it. | [3][f03], [5][f05] |
| Dolphin view buttons | Icons → Compact → Details changed the folder layout. Clicking Name reversed the sort order. | [7][f07], [8][f08], [9][f09], [10][f10] |
| Dolphin scrolling | Wheel movement scrolled the list down; dragging the scrollbar thumb returned it to the top. | [11][f11], [12][f12] |
| Dolphin split panes | Split opened two panes. Places → Desktop changed only the active right pane; Back/Forward returned between Home and Desktop. Close removed the split. | [13][f13], [14][f14], [15][f15], [16][f16], [17][f17] |
| Folder properties | Right-click Videos → Properties opened General; Permissions and Details tabs displayed different information. Cancel returned to Dolphin. | [18][f18], [19][f19], [20][f20], [21][f21] |
| Window controls | Title-bar drag moved Dolphin. Maximize, Restore, Minimize and taskbar restore all produced the expected window state. Close removed the window. | [22][f22], [23][f23], [24][f24], [25][f25], [26][f26], [27][f27] |
| KWrite menus and buttons | File/View menus opened; New created an empty document and expanded the available View actions. Borders → Show Line Numbers removed the visible gutter number. | [29][f29], [31][f31], [32][f32], [33][f33], [35][f35], [36][f36] |
| KWrite file picker | Open displayed the picker; folder activation reached `/etc/fonts`, the Icons button changed its layout, and Cancel returned to the editor. Closing the empty editor returned to the desktop. | [37][f37], [38][f38], [39][f39], [40][f40], [41][f41] |
| Chromium menus | Three-dot menu opened. Zoom `+` changed 100% to 110%; wheel movement and hovering the bottom arrow exposed lower items. A Find and edit submenu also appeared. | [47][f47], [48][f48], [49][f49], [50][f50] |
| Chromium settings | Settings and Appearance rendered. Show home button added a toolbar icon. The Tab position dropdown switched from horizontal tabs to a vertical rail; the rail expanded under the pointer and collapsed when it moved away. | [51][f51], [52][f52], [53][f53], [55][f55], [56][f56], [58][f58], [59][f59] |
| Chromium New Tab | Both tested `+` buttons removed the browser window. The first relaunch showed an abnormal-shutdown prompt. | [44][f44], [45][f45], [60][f60] |
| KDE after browser failure | Kickoff still opened. Hovering Internet displayed Chromium; switching Applications → Places displayed system/place entries. | [61][f61], [62][f62], [64][f64] |
| Calendar | Clicking the clock opened Days. Months and Years buttons changed the view. Returning to Days and clicking next/previous changed September → October → September; an outside click dismissed it. | [65][f65], [66][f66], [67][f67], [68][f68], [69][f69], [70][f70] |

## New Tab reproduction and remaining uncertainty

1. Boot the retained image with the recorded SDL/virgl launch policy.
2. Double-click the Chromium desktop icon. The browser displays `about:blank`.
3. Click the horizontal tab-strip `+` at captured-client coordinate (330,79).
   At **15:55:18.965 UTC**, this action was followed by the window and taskbar
   entry disappearing. Relaunching displayed the abnormal-shutdown prompt.
4. Dismiss that prompt. Open the three-dot menu → Settings → Appearance, then
   use Tab position → Vertical. These views and other tested controls worked.
5. Collapse the tab rail and move away. Click its `+` at (66,235).
   At **16:00:17.210 UTC**, the browser window disappeared again.

These are two observations in one VM session with different tab layouts and
the same browser profile, not independent fresh-boot samples. Multiple-tab
switching and closing could not be validated. The first relaunch supports an
abnormal browser exit associated with New Tab. The second disappearance lacks
independent process-exit evidence. Neither observation proves a kernel or GPU
cause.

The [boot log][runlog] contains a killed `PerfettoTrace` thread record
(`pid=278`, `tgid=270`, `signo=0`, `code=6`, `reason=pending-delivery`) near
the first disappearance. The same event appears in the debug log. Thread
ownership and triggering signal were not established from those lines, and
there is no corresponding second-event record. No kernel panic/fatal-fault,
GPU quarantine or async-timeout marker was found in these two logs.

Track this under **GUI-01**: retain the ordinary desktop launch as the
reproduction, establish same-run browser/thread ownership and exit/assertion
evidence, then reduce the failing New Tab path. A working Settings page and
vertical layout do not validate new-tab creation.

## Hover and capture limits

- Single-point hover did not open Desktop → Icons or KWrite → Borders in
  their first captures; clicking opened both. Hovering Kickoff Utilities left
  Internet selected in capture 63. Other hover paths worked, including
  KWrite's View heading, Kickoff Internet, Chromium's submenu and tab rail,
  and calendar arrow tooltips. This is mixed hover evidence; timing/input
  delivery and toolkit policy were not isolated. Retain it under **DESK-04**.
- The file picker activates folders on a single click: a double click on
  `/etc` traversed two levels to `/etc/fonts`. Capture 54 likewise records a
  Custom home-page radio selection after enabling the home button shifted
  the layout; the actual tab dropdown is capture 55. The gallery annotates
  these outcomes rather than treating filenames as passing assertions.
- All 70 QMP guest screenshots returned `no surface`; all 70 host captures
  succeeded. The continuing fit watcher remained disabled as in the earlier
  audit, with a 1356x897 captured client containing the 1280x800 guest image.
  Some later screenshots include a host notification over the lower-right
  area. No independent guest/host image-pair or exact fitting claim is made.
  [Image hashes][hashes] and [annotated observations][observations] are retained.
- No new HTTP/HTTPS, media, audio, performance or persistence credit is added
  by this mouse exploration. It does not fix or retest the earlier blank
  Konsole shell.

## Cleanup

QMP `quit` ended the owned VM; the launcher was synchronously reaped with exit
code 0. Exact process enumeration returned **zero QEMU processes**. The private
overlay was removed, the helper's unused HTTP fixture server stopped, and the
base filesystem's size and modification time remained unchanged.
See [cleanup receipt][cleanup]. This was host-controlled termination, not an
in-guest shutdown test.

[gallery]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/index.html
[provenance]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/provenance.json
[cmdline]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/qemu-cmdline.json
[actions]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/actions.jsonl
[runlog]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/run.log
[hashes]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/screenshots.json
[observations]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/observations.json
[cleanup]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/cleanup.json

[f01]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/01-desktop-host.png
[f02]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/02-dolphin-icon-hover-host.png
[f03]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/03-desktop-context-menu-host.png
[f04]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/04-desktop-icons-submenu-host.png
[f05]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/05-desktop-icons-options-host.png
[f06]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/06-dolphin-home-host.png
[f07]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/07-dolphin-open-host.png
[f08]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/08-dolphin-compact-view-host.png
[f09]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/09-dolphin-details-view-host.png
[f10]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/10-dolphin-sort-descending-host.png
[f11]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/11-dolphin-wheel-scroll-host.png
[f12]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/12-dolphin-scrollbar-drag-host.png
[f13]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/13-dolphin-split-view-host.png
[f14]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/14-dolphin-independent-pane-host.png
[f15]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/15-dolphin-back-host.png
[f16]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/16-dolphin-forward-host.png
[f17]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/17-dolphin-split-closed-host.png
[f18]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/18-dolphin-folder-context-host.png
[f19]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/19-dolphin-folder-properties-host.png
[f20]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/20-folder-permissions-tab-host.png
[f21]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/21-folder-details-tab-host.png
[f22]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/22-dolphin-window-drag-host.png
[f23]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/23-dolphin-maximized-host.png
[f24]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/24-dolphin-restored-host.png
[f25]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/25-dolphin-minimized-host.png
[f26]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/26-dolphin-taskbar-restore-host.png
[f27]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/27-dolphin-closed-host.png
[f28]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/28-kwrite-welcome-host.png
[f29]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/29-kwrite-file-menu-host.png
[f30]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/30-kwrite-edit-menu-hover-host.png
[f31]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/31-kwrite-view-menu-hover-host.png
[f32]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/32-kwrite-new-button-host.png
[f33]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/33-kwrite-document-view-menu-host.png
[f34]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/34-kwrite-borders-submenu-host.png
[f35]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/35-kwrite-borders-options-host.png
[f36]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/36-kwrite-line-numbers-off-host.png
[f37]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/37-kwrite-open-dialog-host.png
[f38]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/38-file-picker-etc-host.png
[f39]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/39-file-picker-icon-view-host.png
[f40]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/40-file-picker-cancel-host.png
[f41]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/41-kwrite-closed-host.png
[f42]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/42-chromium-open-host.png
[f43]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/43-chromium-launched-host.png
[f44]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/44-chromium-new-tab-host.png
[f45]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/45-chromium-relaunch-host.png
[f46]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/46-chromium-restore-prompt-dismissed-host.png
[f47]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/47-chromium-main-menu-host.png
[f48]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/48-chromium-zoom-button-host.png
[f49]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/49-chromium-menu-scroll-host.png
[f50]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/50-chromium-menu-lower-items-host.png
[f51]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/51-chromium-settings-view-host.png
[f52]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/52-chromium-appearance-view-host.png
[f53]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/53-chromium-home-button-enabled-host.png
[f54]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/54-chromium-tab-position-options-host.png
[f55]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/55-chromium-tab-position-dropdown-host.png
[f56]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/56-chromium-vertical-tabs-host.png
[f57]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/57-chromium-tab-rail-collapsed-host.png
[f58]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/58-chromium-tab-rail-hover-host.png
[f59]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/59-chromium-rail-pointer-away-host.png
[f60]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/60-chromium-new-tab-retry-host.png
[f61]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/61-kickoff-after-browser-failure-host.png
[f62]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/62-kickoff-internet-category-host.png
[f63]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/63-kickoff-utilities-category-host.png
[f64]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/64-kickoff-places-view-host.png
[f65]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/65-panel-calendar-host.png
[f66]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/66-calendar-months-view-host.png
[f67]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/67-calendar-years-view-host.png
[f68]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/68-calendar-next-month-host.png
[f69]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/69-calendar-previous-month-host.png
[f70]: ../build-x86_64/gui-progress-audit/mouse-audit-20260907T154432Z/70-final-desktop-host.png
