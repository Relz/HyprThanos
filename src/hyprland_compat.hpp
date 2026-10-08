#pragma once

#include "hyprland_features.hpp"

#if HYPRTHANOS_RENDER_CONTEXT
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#else
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#endif

#include <hyprland/src/render/pass/TexPassElement.hpp>

#include <optional>
#include <vector>

class IHyprWindowDecoration;
namespace Render {
    class IHyprRenderer;
    class CRenderContext;
    struct SRenderData;
}

namespace HyprThanos::Compat {

#if HYPRTHANOS_RENDER_CONTEXT
    using RenderContext = Render::CRenderContext;
#else
    using RenderContext = Render::IHyprRenderer;
#endif

    Vector2D                           windowRenderOffset(const PHLWINDOW& window);
    Vector2D                           windowReportedSize(const PHLWINDOW& window);
    std::vector<IHyprWindowDecoration*> windowDecorations(const PHLWINDOW& window);
    std::optional<CBox>                 windowPopupGeometry(const PHLWINDOW& window);
    SP<Desktop::View::CPopup>          windowPopupHead(const PHLWINDOW& window);
    bool                               popupDrawable(const SP<Desktop::View::CPopup>& popup);
    bool                               hasUnsupportedTransformers(const PHLWINDOW& window);

    void                               prepareFadeoutTexture(CTexPassElement::SRenderData& data);
    Render::SRenderData&                renderData(RenderContext& context);
    void                               addPassElement(RenderContext& context, UP<IPassElement>&& element);
    void                               removePassElements(Render::IHyprRenderer& renderer, const char* name);
    Mat3x3                             projectDustBox(RenderContext& context, const CBox& box, eTransform textureTransform);
    void                               scissor(RenderContext& context, const pixman_box32* rect, bool transform = true);
    void                               disableScissor();
    void                               setActiveTexture(GLenum texture);
    void                               bindArrayBuffer(GLuint buffer);

}
