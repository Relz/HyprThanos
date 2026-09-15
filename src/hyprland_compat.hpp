#pragma once

#if __has_include(<hyprland/src/workspace/HLWorkspace.hpp>)
#include <hyprland/src/workspace/HLWorkspace.hpp>
#else
#include <hyprland/src/desktop/Workspace.hpp>
#endif

#if __has_include(<hyprland/src/desktop/view/window/Window.hpp>)
#include <hyprland/src/desktop/view/window/Window.hpp>
#else
#include <hyprland/src/desktop/view/Window.hpp>
#endif

#include <hyprland/src/render/pass/TexPassElement.hpp>

#include <optional>
#include <vector>

class IHyprWindowDecoration;
namespace Render {
    class IHyprRenderer;
}

namespace HyprThanos::Compat {

    Vector2D                           windowRenderOffset(const PHLWINDOW& window);
    Vector2D                           windowReportedSize(const PHLWINDOW& window);
    std::vector<IHyprWindowDecoration*> windowDecorations(const PHLWINDOW& window);
    std::optional<CBox>                 windowPopupGeometry(const PHLWINDOW& window);
    SP<Desktop::View::CPopup>          windowPopupHead(const PHLWINDOW& window);
    bool                               popupDrawable(const SP<Desktop::View::CPopup>& popup);
    bool                               hasUnsupportedTransformers(const PHLWINDOW& window);

    void                               prepareFadeoutTexture(CTexPassElement::SRenderData& data);
    Mat3x3                             projectDustBox(Render::IHyprRenderer& renderer, const CBox& box, eTransform textureTransform);
    void                               setActiveTexture(GLenum texture);
    void                               bindArrayBuffer(GLuint buffer);

}
