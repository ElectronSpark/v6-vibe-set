# Runtime deployment source audit

This is a source audit at pinned Chromium commit
`782af9cb30a53f54487e5d2e44738645a8ec457c`, not a completed runtime manifest or
proof that the browser runs. Named-source hashes and the observed non-component
Release GN arguments are retained in
[runtime-deployment-source-audit.json](runtime-deployment-source-audit.json).

The reported 5,172 GN runtime-dependency entries do not by themselves require
5,172 deployed files. The pinned Linux packaging implementation provides a
smaller, explicit deployment policy. `chrome/installer/linux/common/installer.py`
lines 457–571 lists executables and conditional shared libraries; lines 573–695
lists final resource packs, ICU data, scale packs, the selected V8 snapshot,
locale packs and optional preload/default-app assets. Conditional libraries are
also declared in `chrome/installer/linux/BUILD.gn` lines 65–127. Packaging names
and stripping are explicit transformations; preserve their provenance if used.
Do not change the normal browser wrapper or silently reuse frozen-build assets.

The generated DevTools inputs have a concrete packing path:

1. `third_party/devtools-frontend/src/BUILD.gn` lines 128–190 compresses Release
   resources and generates `devtools_resources.grd`.
2. `content/browser/devtools/BUILD.gn` lines 25–50 builds
   `devtools_resources.pak` from that GRD.
3. `chrome/browser/resources/BUILD.gn` lines 402–479 includes the DevTools pack
   in `dev_ui_resources.pak` when the frontend is enabled.
4. `chrome/chrome_paks.gni` lines 120 and 228 includes that pack in the final
   `resources.pak` installed by the Linux packager.

This supports retaining the final packs instead of every loose generated
frontend input. It does not justify deleting arbitrary dependencies by suffix
or taking the first 1,024 entries. Keep an explicit inclusion/exclusion receipt
that links packed inputs to their final artifacts. Build all selected outputs
from the diagnostic checkout. Record actual executable and library hashes;
inspect each deployed ELF's `DT_NEEDED` closure and account for libraries loaded
dynamically, including ANGLE/SwiftShader/Vulkan and any enabled optional shims.
Identify dependencies supplied by the frozen OS image separately. The actual
finished output inventory must confirm completeness and the existing count,
file-size and aggregate limits before staging.

Use the installer policy as the deployment reference, with the existing fixed
runtime path. Running the distribution installer itself would introduce
unrelated launcher/package transformations. The current capture and staging
tools accept an explicit completed file list and refuse excess limits; neither
tool should silently truncate the list. Guest hashing and the emission smoke
are still required after a real build and private-image staging.
