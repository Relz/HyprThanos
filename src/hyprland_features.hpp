#pragma once

// Select the supported API families: the legacy release API and the explicit
// render-context API. Declaration checks in hyprland_compat.cpp validate the hook ABI.
#if __has_include(<hyprland/src/render/Context.hpp>)
#define HYPRTHANOS_RENDER_CONTEXT 1
#else
#define HYPRTHANOS_RENDER_CONTEXT 0
#endif
