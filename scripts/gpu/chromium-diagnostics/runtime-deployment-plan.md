# Diagnostic runtime inclusion plan

This is a source-based deployment plan, not a staged runtime manifest. No
runtime file has been hashed or copied by this audit. Keep the normal
`/bin/wayland-chromium` wrapper, fixed browser path and existing `--no-sandbox`
policy. All selected executable/data files must come from the completed
diagnostic build; generate their actual hashes only after the builder finishes.

The source pin is Chromium `782af9cb30a53f54487e5d2e44738645a8ec457c`.
The builder requests target `chrome` in `out/BottleneckRelease`; exact commands,
concurrency and host scheduling priority are retained in the build receipts.
On Linux, `chrome` resolves to `chrome_initial`; the group documents this at
`chrome/BUILD.gn:132–145`. That executable's resource and helper dependencies
are attached directly, so this spelling does not skip those dependencies.

## Final argument interpretation

The retained final arguments select Linux x64, Release, non-component,
non-official, sysroot, Wayland, Chrome FFmpeg codecs and
`dcheck_always_on=false`. With no overrides for the following values, the pinned
source defaults suggest the following values. Confirm them with the final GN
query; this table does not authorize removing any finished runtime library:

| Value | Result and source |
| --- | --- |
| `use_static_angle` | `true`; `ui/gl/features.gni:10` |
| `enable_swiftshader` | `true`; `ui/gl/features.gni:27–31` |
| `angle_shared_libvulkan` | `true` on Linux; `third_party/angle/gni/angle.gni:186` |
| `use_qt5`, `use_qt6` | `true` for this Linux sysroot build without MSan/Cast; `ui/qt/qt.gni:12–13` |
| `use_custom_libcxx`, `libcxx_is_shared` | `true`, `false`; `build/config/c++/c++.gni:17,77` |
| `icu_use_data_file` | `true`; `third_party/icu/config.gni:16` |
| `v8_use_external_startup_data`, `use_v8_context_snapshot` | `true`, `true`; `v8/gni/v8.gni:302–305` and `tools/v8_context_snapshot/v8_context_snapshot.gni:32–40,71–73` |
| `is_chrome_branded`, `is_chrome_for_testing_branded`, `bundle_widevine_cdm` | `false`; `build/config/chrome_build.gni:12,29` and `third_party/widevine/cdm/widevine.gni:12–16,52–54` |
| `angle_assert_always_on`, `angle_debug_layers_enabled`, `angle_enable_vulkan_validation_layers` | `false`; `third_party/angle/gni/angle.gni:144,176,362–365` |

These are source evaluations, not a new `gn args --list` receipt. The saved
`receipts/runtime-deps-planned.json` is historical planned metadata. The builder
also observed a Vulkan validation-layer object compiling in the final graph
despite `dcheck_always_on=false`; the ANGLE expression alone does not settle
other producers or runtime consumers. Refresh dependency metadata after the
owned build finishes and compare it with this plan. File existence or a compile
step alone does not establish a GN option's value or a deployment requirement.
Non-component Release configuration alone is insufficient to exclude dynamic
ANGLE libraries. Preserve Wayland/GL behavior while resolving these conditions.

## Include from the completed build

Paths below are relative to the fixed `chrome-linux64` runtime directory. Keep
the same relative paths in the host staging tree. The old dependency inventory
contains every item in this table; that is planned closure evidence, not proof
the outputs are complete or valid.

| Runtime paths | Producer and reason | Extra build beyond `chrome` |
| --- | --- | --- |
| `chrome` | `//chrome:chrome_initial`; actual instrumented executable | None |
| `chrome_crashpad_handler` | `//components/crash/core/app:chrome_crashpad_handler`, direct data dependency at `chrome/BUILD.gn:315`; preserve helper availability | None |
| `resources.pak`, `chrome_100_percent.pak`, `chrome_200_percent.pak`, every `locales/*.pak` | `//chrome:packed_resources`, direct public/data dependency at `chrome/BUILD.gn:362–365,1558–1565`; final repacked UI/DevTools/localization resources | None |
| `icudtl.dat` | `//third_party/icu:icudata` / `:copy_icudata`; `third_party/icu/BUILD.gn:473–494`; external ICU data | None advertised by closure |
| `v8_context_snapshot.bin` | `//tools/v8_context_snapshot:v8_context_snapshot` / `:generate_v8_context_snapshot`; `tools/v8_context_snapshot/BUILD.gn:21–26,42–65`; selected external startup snapshot | None advertised by closure; exact producer target is available if independently validating this output |
| `libvk_swiftshader.so`, `vk_swiftshader_icd.json` | `//third_party/swiftshader/src/Vulkan:swiftshader_libvulkan` and `:icd_file`; `ui/gl/BUILD.gn:225–229` | None |
| `libvulkan.so.1` | `//third_party/vulkan-loader/src:libvulkan`; `ui/gl/BUILD.gn:247–249` | None |
| `libqt5_shim.so`, `libqt6_shim.so` | `//ui/qt:qt5_shim`, `:qt6_shim`; `ui/qt/BUILD.gn:137–174` and `ui/linux/BUILD.gn:69–73`; loaded manually, so retain even when absent from direct browser `DT_NEEDED` | None |
| `MEIPreload/manifest.json`, `MEIPreload/preloaded_data.pb` | `//chrome/browser/resources/media/mei_preload:component`; direct data dependency at `chrome/BUILD.gn:373–377` | None |
| `PrivacySandboxAttestationsPreloaded/manifest.json`, `PrivacySandboxAttestationsPreloaded/privacy-sandbox-attestations.dat` | `//components/privacy_sandbox/privacy_sandbox_attestations/preload:component`; same direct dependency list | None |
| `IwaKeyDistribution/manifest.json`, `IwaKeyDistribution/iwa-key-distribution.pb` | `//chrome/browser/web_applications/isolated_web_apps/key_distribution/preload:component`; same direct dependency list. Its BUILD.gn explicitly notes incomplete Linux installer support, so retain these two advertised production data files beyond the installer list | None |
| `resources/accessibility/reading_mode_gdocs_helper_manifest.json` | Explicit non-generated resource in the saved `//chrome:chrome` runtime dependency list; conservatively retain it. Final GN metadata must confirm this path; no claim is made here about its feature being exercised | None advertised by closure |
| `libEGL.so`, `libGLESv2.so` | Keep as candidates until final `use_static_angle`/ANGLE values and finished ELF/dlopen accounting confirm whether these are unused infrastructure stubs or required runtime libraries | None advertised by closure |
| `libVkLayer_khronos_validation.so`, `angledata/VkLayer_khronos_validation.json`, `libVkICD_mock_icd.so`, `angledata/VkICD_mock_icd.json` | Keep as candidates pending refreshed GN/loader accounting. The observed validation-layer compile prevents inferring absence from the ANGLE debug expression | None advertised by historical closure; final closure must confirm |
| `snapshot_blob.bin` | Keep as a candidate until final startup-data policy confirms that the context snapshot is the sole deployed snapshot | None advertised by closure |

The saved list normalizes from 5,172 entries to 5,164 unique paths. Applying
this table gives **254 planned candidate files: 26 non-locale files and 228 locale
packs**, retaining the seven unresolved auxiliary files. This is comfortably
below the 1,024-file cap, without truncation. Deduplication
must normalize only an initial `./` and retain every selected unique path.
Recompute the exact count and sizes from finished outputs before staging.

## Optional helper and excluded outputs

`chrome_sandbox` is the only archive-style helper in this assessment that needs
an additional target: `//sandbox/linux:chrome_sandbox` (Ninja `chrome_sandbox`).
The direct sandbox dependency at `chrome/BUILD.gn:179–180` is ChromeOS-only;
Linux `installer_deps` adds this helper explicitly at lines 460–467. The unchanged
`--no-sandbox` wrapper means it is not required for this fixture: pinned
`content/browser/zygote_host/zygote_host_impl_linux.cc:86–89` returns before
selecting a sandbox binary. If retaining the dormant helper for archive
compatibility, build it from this checkout, preserve its actual mode and hash,
and add it explicitly (255 planned candidate files). Do not enable it or claim sandbox
execution from its presence. The distribution installer renames it and sets
setuid mode; those installer transformations are outside this capture plan.

No additional target is currently required for the media fixture. Do not run
`installer_deps`, `.deb` or `.rpm` packaging merely to obtain the above assets.
The installer-only enterprise management service is not part of this fixture;
building it would require
`//chrome/browser/enterprise/connectors/device_trust/key_management/installer/management_service:chrome_management_service`.
The optional `strip_*` targets are also separate transformations. Use one only
if an actual completed binary needs an explicitly recorded stripping step;
the existing 2-GiB per-file gate must still apply to the bytes being deployed.

| Excluded family | Explicit reason |
| --- | --- |
| 4,874 `gen/third_party/devtools-frontend/...` paths | Build inputs become generated GRD, `devtools_resources.pak`, `dev_ui_resources.pak`, then final `resources.pak`. The exact chain and source hashes are in `runtime-deployment-source-audit.json`; do not deploy both loose build inputs and final packs merely because GN lists both |
| 36 `pyproto/...` paths | `third_party/protobuf/proto_library.gni:18–35,163,503–505,560–565` identifies generated Python protobuf stubs and separate Python-runtime targets for tests. The Linux packager's explicit executable/resource lists contain no pyproto tree. These stubs are not the fixture's Python owner helper |
| Potential later omission: `libEGL.so`, `libGLESv2.so` | **Not excluded yet.** `ui/gl/BUILD.gn:60–84,219–223` generates dummy libraries when static ANGLE is enabled; installer `get_binary_artifacts` includes real ANGLE libraries only otherwise. Require confirmed final `use_static_angle`/ANGLE values and actual finished ELF/dlopen accounting before applying that distinction |
| Potential later omission: validation-layer/mock-ICD libraries and `angledata` JSON | **Not excluded yet.** The Linux installer list does not deploy them, but the active build does compile a validation-layer object. Reconcile all final GN consumers and loader requirements; neither `dcheck_always_on=false` nor compilation alone settles deployment |
| Potential later omission: `snapshot_blob.bin` | **Not excluded yet.** It is input to generating the context snapshot; pinned installer `get_resource_artifacts` at lines 620–637 chooses the context snapshot when present. Confirm the final startup-data policy before omitting the raw snapshot. The generator executable itself remains a build tool |
| `WidevineCdm`, internal optimization/ML shared libraries, `lib/libc++.so` | Corresponding branding/internal/shared-libc++ build conditions are not enabled. Never borrow these from the frozen browser to fill an assumed dependency |
| `chrome-wrapper`, package scripts, icons/desktop integration, source generators, object files | The existing xv6 launcher/desktop integration stays in force. These are build/package integration artifacts, not replacements for the authorized wrapper |

Before actual staging, the builder must supply a complete chosen file list,
actual stable hashes/sizes/modes, final GN arguments and source/patch identities.
Inspect deployed ELF `DT_NEEDED` and dynamic library lookup requirements without
executing them on the host. Keep required external Qt/libstdc++/glibc and other
system libraries attributed to the frozen OS image; do not bundle host paths
implicitly. Resolve unexpected final dependencies explicitly, then apply host
and guest hash gates and the emission smoke. This plan provides no runtime or
performance credit.
