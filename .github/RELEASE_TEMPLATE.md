HyprThanos {{VERSION}} provides a GPU dust-disintegration window close effect for Hyprland.

## Compatibility

- Hyprland {{MIN_HYPRLAND_VERSION}} or newer
- x86_64
- OpenGL renderer
- Non-rotated, non-mirrored SDR/sRGB outputs for the dust effect; unsupported output paths fall back to the stock fade

## Install

```sh
hyprpm update
hyprpm add {{REPOSITORY_URL}}.git
hyprpm enable hyprthanos
hyprpm reload
```

The effect is disabled by default. See the README for [configuration]({{REPOSITORY_URL}}/blob/{{TAG}}/README.md#configure) and [recovery]({{REPOSITORY_URL}}/blob/{{TAG}}/README.md#recovery) instructions.

No prebuilt plugin binary is attached because the Hyprland plugin ABI is unstable. Hyprpm builds the plugin against the matching Hyprland headers.
