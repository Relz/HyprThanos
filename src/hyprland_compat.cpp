#include "hyprland_compat.hpp"

#include "fadeout_hook.hpp"

#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/transformer/MotionBlurTransformer.hpp>

#if HYPRTHANOS_RENDER_CONTEXT
#include "transformer_policy.hpp"
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/render/transformer/TransformerList.hpp>
#else
#include <hyprland/src/protocols/XDGShell.hpp>
#endif

#include <algorithm>
#include <type_traits>

namespace HyprThanos::Compat {

    namespace {
        // A demangled symbol does not encode its return type. Check the full declarations as well.
#if HYPRTHANOS_RENDER_CONTEXT
        using RenderMember = void (Render::IHyprRenderer::*)(Render::CRenderContext&, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE);
        using SceneRenderMember = void (Render::IHyprRenderer::*)(Render::CRenderContext&, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE, Render::eSceneMode);
        // Explicit instantiations are exempt from access checks (C++ [temp.explicit]).
        // This validates the private overload, including its return type, without changing Hyprland's headers.
        template <typename Member, Member Target>
        struct SHookDeclaration {};
        template struct SHookDeclaration<SceneRenderMember, static_cast<SceneRenderMember>(&Render::IHyprRenderer::renderFadeouts)>;
        static_assert(std::is_same_v<RenderFadeoutsFn, void (*)(Render::IHyprRenderer*, Render::CRenderContext&, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE, Render::eSceneMode)>);
#else
        using RenderMember = void (Render::IHyprRenderer::*)(PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE);
        static_assert(std::is_same_v<RenderFadeoutsFn, void (*)(Render::IHyprRenderer*, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE)>);
#endif
        using SnapshotMember = SP<Render::IFramebuffer> (Render::IHyprRenderer::*)(PHLWINDOW);
        using CreateFunction = SP<Desktop::CWindowFadeout> (*)(PHLWINDOW, SP<Render::IFramebuffer>, float);
        static_assert(std::is_same_v<decltype(static_cast<RenderMember>(&Render::IHyprRenderer::renderFadeouts)), RenderMember>);
        static_assert(std::is_same_v<decltype(static_cast<SnapshotMember>(&Render::IHyprRenderer::makeSnapshotFB)), SnapshotMember>);
        static_assert(std::is_same_v<decltype(&Desktop::CWindowFadeout::create), CreateFunction>);
    }

    Vector2D windowRenderOffset(const PHLWINDOW& window) {
#if HYPRTHANOS_RENDER_CONTEXT
        Vector2D offset = window->presentation().floatingOffset();
        const bool pinned = static_cast<bool>(window->m_state & Desktop::View::WINDOW_STATE_PINNED);
#else
        Vector2D offset = window->m_floatingOffset;
        const bool pinned = window->m_pinned;
#endif
        if (!pinned && window->m_workspace)
            offset += window->m_workspace->m_renderOffset->value();
        return offset;
    }

    Vector2D windowReportedSize(const PHLWINDOW& window) {
#if HYPRTHANOS_RENDER_CONTEXT
        return window->backend().reportedSize();
#else
        return window->getReportedSize();
#endif
    }

    std::vector<IHyprWindowDecoration*> windowDecorations(const PHLWINDOW& window) {
        std::vector<IHyprWindowDecoration*> result;
#if HYPRTHANOS_RENDER_CONTEXT
        const auto& source = window->presentation().decorations();
#else
        const auto& source = window->m_windowDecorations;
#endif
        result.reserve(source.size());
        for (const auto& decoration : source)
            result.emplace_back(decoration.get());
        return result;
    }

    std::optional<CBox> windowPopupGeometry(const PHLWINDOW& window) {
#if HYPRTHANOS_RENDER_CONTEXT
        const auto& backend = window->backend();
        if (backend.isX11() || !backend.valid())
            return std::nullopt;
        return backend.geometry().box;
#else
        const auto surface = window->m_xdgSurface.lock();
        if (window->m_isX11 || !surface)
            return std::nullopt;
        return surface->m_current.geometry;
#endif
    }

    SP<Desktop::View::CPopup> windowPopupHead(const PHLWINDOW& window) {
#if HYPRTHANOS_RENDER_CONTEXT
        return window->popupHead();
#else
        return window->m_popupHead;
#endif
    }

    bool popupDrawable(const SP<Desktop::View::CPopup>& popup) {
#if HYPRTHANOS_RENDER_CONTEXT
        return popup && popup->mapped() && popup->acceptsInput() && popup->alphaNonZero() && !popup->inert();
#else
        return popup && popup->m_mapped && !popup->inert();
#endif
    }

    bool hasUnsupportedTransformers(const PHLWINDOW& window) {
#if HYPRTHANOS_RENDER_CONTEXT
        const auto& list = window->effects().transformers();
        if (!list)
            return true;

        // Only the number of active stages is used; these are logical window-space boxes.
        return Detail::hasUnsupportedActiveTransformers<Render::CMotionBlurTransformer>(*list, window->getFullWindowBoundingBox());
#else
        return std::ranges::any_of(window->m_transformers, [](const auto& transformer) {
            return !transformer || dynamic_cast<Render::CMotionBlurTransformer*>(transformer.get()) == nullptr;
        });
#endif
    }

    void prepareFadeoutTexture(CTexPassElement::SRenderData& data) {
#if HYPRTHANOS_RENDER_CONTEXT
        data.blurShapeInvalid = true;
#else
        data.flipEndFrame = true;
#endif
    }

    Render::SRenderData& renderData(RenderContext& context) {
#if HYPRTHANOS_RENDER_CONTEXT
        return context.m_data;
#else
        return context.m_renderData;
#endif
    }

    void addPassElement(RenderContext& context, UP<IPassElement>&& element) {
#if HYPRTHANOS_RENDER_CONTEXT
        Render::IHyprRenderer::addPassElement(context, std::move(element));
#else
        context.addPassElement(std::move(element));
#endif
    }

    void removePassElements(Render::IHyprRenderer& renderer, const char* name) {
#if HYPRTHANOS_RENDER_CONTEXT
        auto& context = renderer.context();
        Render::IHyprRenderer::currentPass(context).removeAllOfType(name);
        context.m_pass.removeAllOfType(name);
#else
        renderer.currentPass().removeAllOfType(name);
        renderer.m_renderPass.removeAllOfType(name);
#endif
    }

    Mat3x3 projectDustBox(RenderContext& context, const CBox& box, eTransform textureTransform) {
#if HYPRTHANOS_RENDER_CONTEXT
        return g_pHyprRenderer->projectBoxToTarget(context, box, Math::invertTransform(textureTransform));
#else
        if (context.monitorTransformEnabled()) {
            const auto inverse = Math::wlTransformToHyprutils(Math::invertTransform(context.m_renderData.pMonitor->m_transform));
            textureTransform = Math::composeTransform(inverse, textureTransform);
        }
        return context.projectBoxToTarget(box, textureTransform);
#endif
    }

    void scissor(RenderContext& context, const pixman_box32* rect, bool transform) {
#if HYPRTHANOS_RENDER_CONTEXT
        Render::GL::g_pHyprOpenGL->scissor(context, rect, transform);
#else
        (void)context;
        Render::GL::g_pHyprOpenGL->scissor(rect, transform);
#endif
    }

    void disableScissor() {
#if HYPRTHANOS_RENDER_CONTEXT
        Render::GL::g_pHyprOpenGL->disableScissor();
#else
        Render::GL::g_pHyprOpenGL->scissor(nullptr);
#endif
    }

    void setActiveTexture(GLenum texture) {
#if HYPRTHANOS_RENDER_CONTEXT
        Render::GL::g_pHyprOpenGL->setActiveTexture(texture);
#else
        glActiveTexture(texture);
#endif
    }

    void bindArrayBuffer(GLuint buffer) {
#if HYPRTHANOS_RENDER_CONTEXT
        Render::GL::g_pHyprOpenGL->bindArrayBuffer(buffer);
#else
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
#endif
    }

}
