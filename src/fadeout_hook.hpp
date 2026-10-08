#pragma once

#include "hyprland_features.hpp"

#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <string>

namespace HyprThanos {

#if HYPRTHANOS_RENDER_CONTEXT
    using RenderFadeoutsFn = void (*)(Render::IHyprRenderer*, Render::CRenderContext&, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE, Render::eSceneMode);
#else
    using RenderFadeoutsFn = void (*)(Render::IHyprRenderer*, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE);
#endif

    bool installFadeoutHook();
    const std::string& fadeoutHookError();
    void removeFadeoutHook() noexcept;

#if HYPRTHANOS_RENDER_CONTEXT
    void renderFadeoutsHook(Render::IHyprRenderer* renderer, Render::CRenderContext& context, PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace,
                           Render::eSceneMode mode) noexcept;
#else
    void renderFadeoutsHook(Render::IHyprRenderer* renderer, PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace) noexcept;
#endif

}
