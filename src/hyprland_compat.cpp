#include "hyprland_compat.hpp"

#include "fadeout_hook.hpp"
#include "transformer_policy.hpp"

#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/transformer/MotionBlurTransformer.hpp>

#if __has_include(<hyprland/src/desktop/view/window/WindowPresentation.hpp>)
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#endif
#if __has_include(<hyprland/src/desktop/view/window/WindowEffectsController.hpp>)
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#endif
#if __has_include(<hyprland/src/render/transformer/TransformerList.hpp>)
#include <hyprland/src/render/transformer/TransformerList.hpp>
#endif

#include <algorithm>
#include <type_traits>

namespace HyprThanos::Compat {

    namespace {
        // A demangled symbol does not encode its return type. Check the full declarations as well.
        using RenderMember = void (Render::IHyprRenderer::*)(PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE);
        using SnapshotMember = SP<Render::IFramebuffer> (Render::IHyprRenderer::*)(PHLWINDOW);
        using CreateFunction = SP<Desktop::CWindowFadeout> (*)(PHLWINDOW, SP<Render::IFramebuffer>, float);
        static_assert(std::is_same_v<decltype(&Render::IHyprRenderer::renderFadeouts), RenderMember>);
        static_assert(std::is_same_v<decltype(static_cast<SnapshotMember>(&Render::IHyprRenderer::makeSnapshotFB)), SnapshotMember>);
        static_assert(std::is_same_v<decltype(&Desktop::CWindowFadeout::create), CreateFunction>);
        static_assert(std::is_same_v<RenderFadeoutsFn, void (*)(Render::IHyprRenderer*, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE)>);

        template <typename Window>
        Vector2D floatingOffset(Window& window) {
            if constexpr (requires { window.presentation().floatingOffset(); })
                return window.presentation().floatingOffset();
            else
                return window.m_floatingOffset;
        }

        template <typename Window>
        Vector2D reportedSize(Window& window) {
            if constexpr (requires { window.backend().reportedSize(); })
                return window.backend().reportedSize();
            else
                return window.getReportedSize();
        }

        template <typename Window>
        decltype(auto) decorations(Window& window) {
            if constexpr (requires { window.presentation().decorations(); })
                return window.presentation().decorations();
            else
                return (window.m_windowDecorations);
        }

        template <typename Window>
        std::optional<CBox> popupGeometry(Window& window) {
            if constexpr (requires { window.backend().geometry(); }) {
                const auto& backend = window.backend();
                if (backend.isX11() || !backend.valid())
                    return std::nullopt;
                return backend.geometry().box;
            } else {
                const auto surface = window.m_xdgSurface.lock();
                if (window.m_isX11 || !surface)
                    return std::nullopt;
                return surface->m_current.geometry;
            }
        }

        template <typename Window>
        SP<Desktop::View::CPopup> popupHead(Window& window) {
            if constexpr (requires { window.popupHead(); })
                return window.popupHead();
            else
                return window.m_popupHead;
        }

        template <typename Popup>
        bool drawable(Popup& popup) {
            if constexpr (requires { popup.mapped(); popup.acceptsInput(); popup.alphaNonZero(); })
                return popup.mapped() && popup.acceptsInput() && popup.alphaNonZero() && !popup.inert();
            else
                return popup.m_mapped && !popup.inert();
        }

        template <typename Window>
        bool unsupportedTransformers(Window& window) {
            if constexpr (requires { window.effects().transformers(); }) {
                const auto& list = window.effects().transformers();
                if (!list)
                    return true;

                // Only the number of active stages is used; these are logical window-space boxes.
                return Detail::hasUnsupportedActiveTransformers<Render::CMotionBlurTransformer>(*list, window.getFullWindowBoundingBox());
            } else {
                return std::ranges::any_of(window.m_transformers, [](const auto& transformer) {
                    return !transformer || dynamic_cast<Render::CMotionBlurTransformer*>(transformer.get()) == nullptr;
                });
            }
        }

        template <typename Data>
        void prepareTexture(Data& data) {
            if constexpr (requires { data.flipEndFrame; })
                data.flipEndFrame = true;
            if constexpr (requires { data.blurShapeInvalid; })
                data.blurShapeInvalid = true;
        }

        template <typename Renderer, typename Transform>
        Mat3x3 projectBox(Renderer& renderer, const CBox& box, Transform textureTransform) {
            if constexpr (requires { renderer.monitorTransformEnabled(); }) {
                if (renderer.monitorTransformEnabled()) {
                    const auto inverse = Math::wlTransformToHyprutils(Math::invertTransform(renderer.m_renderData.pMonitor->m_transform));
                    textureTransform = Math::composeTransform(inverse, textureTransform);
                }
            } else {
                // Modern renderTextureInternal applies the inverse wl_surface buffer transform.
                textureTransform = Math::invertTransform(textureTransform);
            }
            return renderer.projectBoxToTarget(box, textureTransform);
        }

        template <typename GL>
        void activeTexture(GL& gl, GLenum texture) {
            if constexpr (requires { gl.setActiveTexture(texture); })
                gl.setActiveTexture(texture);
            else
                glActiveTexture(texture);
        }

        template <typename GL>
        void arrayBuffer(GL& gl, GLuint buffer) {
            if constexpr (requires { gl.bindArrayBuffer(buffer); })
                gl.bindArrayBuffer(buffer);
            else
                glBindBuffer(GL_ARRAY_BUFFER, buffer);
        }
    }

    Vector2D windowRenderOffset(const PHLWINDOW& window) {
        Vector2D offset = floatingOffset(*window);
#if __has_include(<hyprland/src/desktop/view/window/Window.hpp>)
        const bool pinned = static_cast<bool>(window->m_state & Desktop::View::WINDOW_STATE_PINNED);
#else
        const bool pinned = window->m_pinned;
#endif
        if (!pinned && window->m_workspace)
            offset += window->m_workspace->m_renderOffset->value();
        return offset;
    }

    Vector2D windowReportedSize(const PHLWINDOW& window) {
        return reportedSize(*window);
    }

    std::vector<IHyprWindowDecoration*> windowDecorations(const PHLWINDOW& window) {
        std::vector<IHyprWindowDecoration*> result;
        const auto& source = decorations(*window);
        result.reserve(source.size());
        for (const auto& decoration : source)
            result.emplace_back(decoration.get());
        return result;
    }

    std::optional<CBox> windowPopupGeometry(const PHLWINDOW& window) {
        return popupGeometry(*window);
    }

    SP<Desktop::View::CPopup> windowPopupHead(const PHLWINDOW& window) {
        return popupHead(*window);
    }

    bool popupDrawable(const SP<Desktop::View::CPopup>& popup) {
        return popup && drawable(*popup);
    }

    bool hasUnsupportedTransformers(const PHLWINDOW& window) {
        return unsupportedTransformers(*window);
    }

    void prepareFadeoutTexture(CTexPassElement::SRenderData& data) {
        prepareTexture(data);
    }

    Mat3x3 projectDustBox(Render::IHyprRenderer& renderer, const CBox& box, eTransform textureTransform) {
        return projectBox(renderer, box, textureTransform);
    }

    void setActiveTexture(GLenum texture) {
        activeTexture(*Render::GL::g_pHyprOpenGL, texture);
    }

    void bindArrayBuffer(GLuint buffer) {
        arrayBuffer(*Render::GL::g_pHyprOpenGL, buffer);
    }

}
