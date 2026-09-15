#!/usr/bin/env bash
set -euo pipefail

: "${HYPRLAND_FLAKE:?Set HYPRLAND_FLAKE to an immutable Hyprland flake reference}"
COMPAT_BUILD_DIR="${1:-build/compat}"
NIX_ARGS=(--no-write-lock-file)
if [[ -n "${NIXPKGS_REF:-}" ]]; then
    NIX_ARGS+=(--override-input nixpkgs "github:NixOS/nixpkgs/${NIXPKGS_REF}")
fi

printf 'Hyprland source: %s\n' "$HYPRLAND_FLAKE"
outputs="$(nix build "${NIX_ARGS[@]}" --no-link --json "${HYPRLAND_FLAKE}#hyprland^out,dev")"
COMPAT_HYPRLAND_DEV="$(jq -er '.[0].outputs.dev' <<< "$outputs")"
COMPAT_HYPRLAND_OUT="$(jq -er '.[0].outputs.out' <<< "$outputs")"

# Private pkg-config requirements of GLib/Pango/hyprgraphics are not all propagated by nixpkgs. Resolve
# them from the same (possibly overridden) input as Hyprland.
archive="$(nix flake archive "${NIX_ARGS[@]}" --json "$HYPRLAND_FLAKE")"
nixpkgs_path="$(jq -er '.inputs.nixpkgs.path' <<< "$archive")"
# A metadata-only shell also follows propagated development outputs (for example
# librsvg -> gdk-pixbuf -> libtiff). Its compiler/flags never replace Hyprland's.
private_environment="$(nix print-dev-env --impure --json --expr "
    let
        pkgs = (builtins.getFlake \"path:${nixpkgs_path}\").legacyPackages.\${builtins.currentSystem};
        roots = with pkgs; [ glib pango librsvg libjxl libheif file libthai libselinux ];
        privateInputs = pkgs.lib.concatMap
            (p: (p.buildInputs or []) ++ (p.propagatedBuildInputs or [])) roots;
    in pkgs.mkShellNoCC {
        nativeBuildInputs = [ pkgs.pkg-config ];
        buildInputs = builtins.filter pkgs.lib.isDerivation (roots ++ privateInputs);
    }")"
COMPAT_EXTRA_PKGCONFIG="$(jq -er '.variables.PKG_CONFIG_PATH.value' <<< "$private_environment")"

# Import only exported variables: the shell-script form also executes shellHook.
# Keep the caller's HOME and provide live temporary paths instead of the finished
# environment derivation's build directory.
environment_json="$(nix print-dev-env "${NIX_ARGS[@]}" --json "$HYPRLAND_FLAKE")"
environment="$(jq -r '.variables | to_entries[] |
    select(.value.type == "exported") |
    select(.key != "shellHook" and .key != "HOME" and .key != "PWD") |
    "export \(.key)=\(.value.value | @sh)"' <<< "$environment_json")"
COMPAT_TMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$COMPAT_TMPDIR"' EXIT
eval "$environment"
set -euo pipefail
export TMPDIR="$COMPAT_TMPDIR" TMP="$COMPAT_TMPDIR" TEMP="$COMPAT_TMPDIR" TEMPDIR="$COMPAT_TMPDIR" NIX_BUILD_TOP="$COMPAT_TMPDIR"
export NIX_ENFORCE_PURITY=0
export PKG_CONFIG_PATH="$COMPAT_HYPRLAND_DEV/share/pkgconfig:${PKG_CONFIG_PATH:-}:$COMPAT_EXTRA_PKGCONFIG"

pkg-config --modversion hyprland
pkg-config --print-errors --cflags --libs hyprland > /dev/null
pkg-config --print-errors --static --cflags --libs hyprland > /dev/null
cmake --fresh -S . -B "$COMPAT_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DHYPRTHANOS_BUILD_TESTS=ON \
    -DHYPRTHANOS_HYPRLAND_EXECUTABLE="$COMPAT_HYPRLAND_OUT/bin/Hyprland"
cmake --build "$COMPAT_BUILD_DIR" --parallel 2
ctest --test-dir "$COMPAT_BUILD_DIR" --output-on-failure
