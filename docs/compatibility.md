# Compatibility development

## Targets and ABI

The same source is compiled separately against each target's headers and dependencies:

| Target | Hyprland revision | Nix environment |
| --- | --- | --- |
| Minimum supported release | `v0.56.2` / `efb50993780079460b0cbed1363e2166a2de1d9f` | Override nixpkgs to `12d28633cf8e2899ac83315a7e5495fda119d193` for glaze 7 |
| Explicit render-context baseline | `19fb395d45314960e6f79f17994a84094f1cd4f6` | This revision's own locked inputs |

The full build-time/server ABI comparison runs before configuration registration or hook installation. A binary belongs to its build environment and must be rebuilt when the compositor's ABI changes.

The CMake minimum remains `0.56.2`, or a development revision with `GIT_COMMITS >= 7661`. Development builds may still report `0.56.0` through pkg-config. The commit-count check establishes a minimum; the actual header declarations determine API compatibility.

## Adapter boundaries

`src/hyprland_compat.hpp/.cpp` contains the version-dependent operations for two supported API families: the legacy release API and the explicit render-context API. `src/hyprland_features.hpp` selects the family through the presence of `render/Context.hpp`; window/workspace includes, accessors, hook signatures, texture metadata, and GL operations follow that selection. Intermediate development snapshots with the newer window/workspace API but without render contexts are outside these supported families.

| Operation | Release API (`v0.56.2`) | Render-context API (`19fb395`) |
| --- | --- | --- |
| Workspace | `desktop/Workspace.hpp`, `CWorkspace` | `workspace/HLWorkspace.hpp`, `Workspace::CHLWorkspace` |
| Window | `desktop/view/Window.hpp` | `desktop/view/window/Window.hpp` |
| Floating offset and decorations | Window fields | `presentation()` |
| Pinned state | `m_pinned` | `WINDOW_STATE_PINNED` in `m_state` |
| Reported size and popup geometry | `getReportedSize()`, XDG surface | `backend().reportedSize()`, `backend().geometry()` |
| Popup traversal and visibility | `m_popupHead`, `m_mapped` | `popupHead()`, `mapped()`, input/alpha visibility |
| Transformers | Window's transformer vector | `effects().transformers()` |
| Native fadeout texture | `flipEndFrame = true` | `blurShapeInvalid = true` |
| Projection | Monitor-transform stack | Context target projection and inverse buffer transform |
| GL texture unit and array buffer | Direct GL calls | Hyprland's cached state setters |
| Logging | Variadic format arguments | Variadic overload also takes a source location; `compat_log.hpp` preformats and uses the common two-argument overload |

With the explicit context API, pass overrides receive `Render::CRenderContext&`. Damage, clipping, render modifiers, projection, scissoring, and pass submission use that supplied context; they do not substitute the renderer's default context. Unload cleanup removes dust elements from both the renderer's current and primary context passes before plugin resources are destroyed.

The context baseline's window render paths call the private `renderFadeouts(..., Render::eSceneMode)` overload directly. The hook targets that exact overload rather than the public wrapper. Non-monitor scenes and read-only effect contexts use the native implementation without consuming live fadeout captures.

Snapshot bounds stay in monitor-local physical pixels. Popup geometry, decoration extents, workspace offsets, and subsurface scaling are read while the window still exists, immediately after the native snapshot operation. Only weak window references and captured bounds survive that operation.

The modern transformer list exposes one plan stage per active transformer, without exposing its container. `src/transformer_policy.hpp` allows an empty active list or exactly one native motion-blur stage. Finding motion blur alone does not make a list eligible: an additional active transformer, including wobble or a custom motion-blur subclass, uses the native fadeout. Inactive stages do not alter the snapshot.

`src/hook_contract.hpp` defines the exact signatures used for runtime hook resolution: window `makeSnapshotFB`, `CWindowFadeout::create`, and `renderFadeouts` with the selected workspace and context types. Compile-time checks also cover return types, which demangled function names do not encode. The private scene-mode overload is checked through an explicit template instantiation, which C++ exempts from access control; Hyprland's headers are not modified. Runtime resolution requires one matching non-null address and reports all candidates on failure. A failed partial installation rolls back the hooks.

## Compatibility builds

For the locally installed compositor:

```sh
cmake --fresh -S . -B build/compat-local -DCMAKE_BUILD_TYPE=Release
cmake --build build/compat-local --parallel 2
```

Compatibility CI checks pkg-config metadata, then configures and builds the plugin against each target's SDK. ABI comparison and exact hook-symbol resolution run when the plugin is loaded into the compositor.

For reproducible Nix builds, install Nix with `nix-command` and `flakes` enabled, plus `jq`, then run from the repository root:

```sh
HYPRLAND_FLAKE=github:hyprwm/Hyprland/v0.56.2 \
NIXPKGS_REF=12d28633cf8e2899ac83315a7e5495fda119d193 \
  bash .github/scripts/compatibility.sh build/compat-baseline

HYPRLAND_FLAKE='git+https://github.com/hyprwm/Hyprland?ref=main&rev=19fb395d45314960e6f79f17994a84094f1cd4f6&shallow=0' \
  bash .github/scripts/compatibility.sh build/compat-context
```

The script imports the exported compiler/dependency variables from `nix print-dev-env --json`, preserves the caller's home directory, and supplies live temporary paths for the build. It supplies the missing private development metadata for GLib/Pango/hyprgraphics (including `sysprof-capture-4`) from the same resolved nixpkgs input, after the upstream environment's own pkg-config paths. Both dynamic and static pkg-config queries are checked before CMake. The upstream repository-maintenance shell hook is not executed. The Git fetcher retains the commit count required for development-version detection.

Both pinned targets run for pushes, PRs, reusable workflow calls, schedules, and manual runs. The two rolling targets run on schedules and manual runs. CI uploads the CMake configuration log. Releases require successful builds against both pinned targets.

## Runtime smoke check

Use matching plugin/compositor binaries and the [manual loading commands](../README.md#manual-loading) in a separate compositor instance with a usable EGL/GBM renderer. Exercise repeated load/unload, tiled/floating/pinned window closes, unloading during an active fadeout, and disabled-mode fallback. Check the compositor log for shader initialization and circuit-breaker activation, and inspect rendering for orientation, bounds, clipping, and residual pixels.

Before declaring a new revision fully runtime-validated, also exercise these scenes on all supported targets:

| Scene | Expected result |
| --- | --- |
| Wayland and XWayland; tiled, floating, pinned | Correct content and workspace offset |
| Popups and subsurfaces outside the main surface; borders and shadows | Complete captured bounds |
| Non-origin monitors and scales 1, 1.25, 2 | Correct pixel-space position, clipping, and damage |
| Native motion blur alone | Eligible dust snapshot |
| Wobble/custom transformers and custom decorations | Native fadeout |
| Rotated/mirrored or HDR/color-managed outputs | Native fadeout |
| `no_anim`, missing snapshot, disabled mode, `max_active` limit | Native behavior |
| Concurrent closes, workspace changes, output removal, unloading mid-effect | Valid lifetimes and clean rendering afterward |

### Verification record — 2026-09-14

| Target | Build and checks | Runtime smoke |
| --- | --- | --- |
| `v0.56.2` | Installed SDK, GCC 16.2.1; policy and real-binary symbol checks passed | Passed, including all six Wayland/XWayland scenes and three native-fallback scenes |
| `1b85c7a` | Locked Nix SDK, GCC 16.2.0; complete CI script, policy and real-binary symbol checks passed | Passed in a separate nested instance, with the same scenes |

The modern instance also rejected the baseline-built library with an explicit ABI-mismatch error, then loaded its matching library successfully. Colored XWayland screenshots were inspected for orientation and position at scales 1, 1.25, and 2; after-effect screenshots had no residual particles. The extended popup/subsurface, custom-effect, mirroring/HDR, and concurrency matrix above still needs dedicated manual coverage.

## Updating a baseline or release pin

1. Resolve upstream `main` to an immutable SHA and compare the used headers and native snapshot/fadeout implementations.
2. Add capability-specific adaptations, run all pinned builds, and manually check the runtime scenes above.
3. Update the render-context baseline SHA in the workflow and this document once verified.
4. During release preparation, review `hyprpm.toml`'s `commit_pins`. Its current `v0.56.2` entry deliberately selects historical plugin commit `bf242ebf0dc74c84552a0e5c2520198c9d6de361`. Set a replacement only to an existing plugin commit that has passed validation; an uncommitted working tree cannot be the pin target.

Users on an unpinned development revision receive the latest plugin Git source through hyprpm. Users on the pinned release receive the selected historical source until its pin is updated.
