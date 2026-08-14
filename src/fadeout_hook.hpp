#pragma once

#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <string>

namespace HyprThanos {

    using RenderFadeoutsFn = void (*)(Render::IHyprRenderer*, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE);

    bool installFadeoutHook();
    const std::string& fadeoutHookError();
    void removeFadeoutHook() noexcept;

    void renderFadeoutsHook(Render::IHyprRenderer* renderer, PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace) noexcept;

}
