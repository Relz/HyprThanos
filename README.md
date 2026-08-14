# HyprThanos

HyprThanos is a minimal Hyprland plugin that replaces an eligible window's normal close fade with a GPU dust-disintegration effect. It reuses Hyprland's native window snapshot, `windowsOut` geometry, `fadeOut` progress, z-order, dim/blur metadata, damage scheduling, and cleanup. On the custom path, the snapshot is rendered only through source-space grain instances with continuously staggered release times; no separate fading copy, second snapshot, or closed `CWindow` is retained. Rendering failures fall back to the stock texture fade.

## Compatibility

HyprThanos requires:

- Hyprland `0.56.2` or newer
- x86_64 and the OpenGL renderer

Hyprland `0.56.2` is the validated baseline. Newer revisions are supported on a best-effort basis when the plugin still builds and its runtime capability checks pass. An incompatible API change fails the build or rejects the load until the plugin is adapted.

Hyprland plugins exchange internal C++ objects and do not have a stable ABI. Rebuild this plugin against the exact headers for the installed compositor after every Hyprland or linked Hyprland-library update. A binary built for one ABI must not be reused with another. Before hooks or configuration values are registered, the plugin compares the complete build-time and server ABI strings and rejects a mismatch. It also requires one exact target for each internal function hook and rolls back a partial hook installation on failure.

The dust effect currently works only on non-rotated, non-mirrored SDR/sRGB displays. Unsupported display configurations automatically fall back to Hyprland's normal fadeout animation.

## Build

Install Hyprland development headers, CMake, pkg-config, a C++23 compiler compatible with the Hyprland build, and GLESv2 development files. Then run:

```sh
cmake --fresh -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

The result is `build/libhyprthanos.so`. For a release build, use `-DCMAKE_BUILD_TYPE=Release`. Always use a fresh build directory after updating Hyprland so CMake cannot retain an older pkg-config result.

Verify the required entry points with:

```sh
nm -D --defined-only build/libhyprthanos.so | c++filt | grep -E 'plugin(APIVersion|Init|Exit)'
```

## Hyprpm

To let Hyprland's plugin manager build and load the plugin from a Git checkout, install the repository and enable the plugin:

```sh
hyprpm update
hyprpm add https://github.com/Relz/HyprThanos.git
hyprpm enable hyprthanos
hyprpm reload
```

After updating Hyprland, run:

```sh
hyprpm update
```

When the installed Hyprland ABI changes, `hyprpm update` refreshes the matching headers and rebuilds every managed plugin even if its repository has no new commit. After updating only a linked Hyprland library, use `hyprpm update --force`; patch-level dependency versions are not all represented in Hyprland's ABI string. These commands are not invoked automatically by a system package upgrade. Restart Hyprland when `hyprpm` reports that the running compositor still uses the previous ABI.

Enabled hyprpm plugins must be reloaded when Hyprland starts. With the standard system paths, add this to the Lua configuration; adjust the permission path if `hyprpm` is installed elsewhere:

```lua
hl.permission("/usr/(bin|local/bin)/hyprpm", "plugin", "allow")

hl.on("hyprland.start", function()
    hl.exec_cmd("hyprpm reload")
end)
```

## Configure

The effect is disabled by default. With Hyprland's Lua configuration, load a manually built plugin before setting its typed values:

```lua
hl.plugin.load("/absolute/path/to/hyprthanos/build/libhyprthanos.so")

if hl.get_config("plugin.hyprthanos.enabled") ~= nil then
    hl.config({
        plugin = {
            hyprthanos = {
                enabled = true,
                grain_size = 3.0,
                spread = 0.12,
                lateral_repulsion = 0.35,
                direction_x = 0.5,
                direction_y = -0.35,
                turbulence = 0.5,
                max_active = 8,
                max_particles = 131072,
            },
        },
    })
end
```

The guard skips plugin-owned keys during the first parse. Hyprland loads the plugin and reparses the configuration, at which point the values exist and are applied. When `hyprpm` manages loading, omit the `hl.plugin.load(...)` line and keep the guard.

Configuration ranges:

| Setting | Default | Range |
|---|---:|---:|
| `enabled` | `false` | boolean |
| `grain_size` | `3.0` | `1.0..16.0` |
| `spread` | `0.12` | `0.0..0.35` |
| `lateral_repulsion` | `0.35` | `0.0..1.0` |
| `direction_x` | `0.5` | `-1.0..1.0` |
| `direction_y` | `-0.35` | `-1.0..1.0` |
| `turbulence` | `0.5` | `0.0..1.0` |
| `max_active` | `8` | `1..32` |
| `max_particles` | `131072` | `4096..262144` |

`lateral_repulsion` fans grains away from the window center across the configured travel direction. `0` disables this center-based fan and preserves the previous unfanned motion, while `1` bends the outermost grains by up to roughly 45 degrees without increasing their maximum travel distance.

`turbulence` bends each grain along an individual seeded curve. `0` keeps the center-based fan straight, while higher values increase the variation between the middle and end of particle paths without expanding the configured displacement envelope.

`grain_size` is the requested minimum grain edge in physical pixels. If the captured window surface trees, decorations, and popup surface trees would exceed `max_particles`, the renderer increases the effective grain size for that fadeout. This keeps the complete captured content inside the particle field while maintaining a predictable vertex budget.

There is no separate duration setting. Continuous deterministic release times compress each grain's decelerating curved movement, local rotation, and individual fade into Hyprland's native `fadeOut` animation. Windows closed under `no_anim`, or without a valid native snapshot, receive no dust effect.

## Manual Loading

For one-session testing, use an absolute path:

```sh
hyprctl plugin load /absolute/path/to/build/libhyprthanos.so
hyprctl plugin list
hyprctl plugin unload /absolute/path/to/build/libhyprthanos.so
```

Do not load a development binary manually while the same plugin is also managed by `hl.plugin.load` or `hyprpm`; Hyprland rejects duplicate loads.

## Recovery

The plugin fails closed: disabled mode, unsupported outputs, overload, invalid textures, shader compilation failure, and GL errors use the stock fadeout. Shader, preparation, and GL failures latch a circuit breaker so following frames are stock-rendered.

If the graphical session becomes unusable, switch to a TTY, remove or comment out the `hl.plugin.load(...)` line, or disable a hyprpm-managed installation with `hyprpm disable hyprthanos`, and restart Hyprland or the user session. For a responsive compositor, unload directly:

```sh
hyprctl plugin unload /absolute/path/to/build/libhyprthanos.so
```

## Releases

GitHub releases are published automatically when a tag matching `vX.Y.Z` is pushed. The tag must match the project version in `CMakeLists.txt`; prerelease suffixes and leading zeroes are not supported.

1. Update the project version in `CMakeLists.txt` and review `commit_pins` in `hyprpm.toml`. Pins are not updated automatically; a pinned Hyprland revision will continue to use its selected plugin commit.
2. Build and test the plugin with a compatible Hyprland installation, then commit and push the release preparation to `main`.
3. Create and push an annotated tag on that release commit. For example, after setting the project version to `0.1.1`:

```sh
git tag -a v0.1.1 -m "HyprThanos 0.1.1"
git push origin v0.1.1
```

The `Release` workflow validates the version, calls the reusable `Compatibility` workflow, and publishes the release with templated release notes and a commit changelog only after the Hyprland `v0.56.2` baseline passes. Branch pushes and pull requests run compatibility checks without publishing; scheduled and manual compatibility runs also probe the latest Hyprland release and `main`.

Release descriptions use [`.github/RELEASE_TEMPLATE.md`](.github/RELEASE_TEMPLATE.md) from the tagged commit. The version, repository URL, and tag-specific README links are filled in automatically; the minimum Hyprland version comes from the tagged `CMakeLists.txt`. Edit the template only when the shared description, installation instructions, or compatibility restrictions change, not for every release.

The `Changes` section lists commit subjects and links, not pull requests. The previous tag is the highest stable `vX.Y.Z` version lower than the current tag; prerelease and unrelated tag names are ignored. All commits in `previous..current`, including merge commits, are listed oldest first. The first release includes its full history.

To preview the complete description locally after creating a tag, with full history and all tags available:

```sh
bash .github/scripts/release-notes.sh v0.1.1 Relz/HyprThanos
```

The preview only prints Markdown; it does not create or modify a GitHub release. Rendering fails if the minimum Hyprland version cannot be read or template placeholders remain unresolved.

Treat pushed release tags as immutable. For transient CI or API failures, rerun the workflow on the same commit; if the release code needs changes, prepare a new version and tag. The publishing job rechecks the remote tag against the checked commit, and an existing release is left unchanged on reruns. Protect `v*` tags against updates and deletion in the repository rulesets to prevent changes between that check and publication.

Publication uses the built-in `GITHUB_TOKEN`, with `contents: write` granted only to the publishing job; no personal access token is required.

Releases contain GitHub's source archives, not a prebuilt `.so`: the plugin must be built against the installed Hyprland ABI. `hyprpm` builds from Git rather than downloading release assets.

## License

HyprThanos is licensed under the GNU General Public License v3.0 or later (`GPL-3.0-or-later`). See `LICENSE`.
