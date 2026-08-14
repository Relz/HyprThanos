#include "fadeout_hook.hpp"

#include "dust_pass.hpp"
#include "state.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FadingOutState.hpp>
#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/transformer/MotionBlurTransformer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace HyprThanos {

    namespace {
        using MakeWindowSnapshotFn = SP<Render::IFramebuffer> (*)(Render::IHyprRenderer*, PHLWINDOW);
        using CreateWindowFadeoutFn = SP<Desktop::CWindowFadeout> (*)(PHLWINDOW, SP<Render::IFramebuffer>, float);

        RenderFadeoutsFn      g_original         = nullptr;
        MakeWindowSnapshotFn  g_originalSnapshot = nullptr;
        CreateWindowFadeoutFn g_originalCreate   = nullptr;
        std::string           g_installError;

        bool isWindowPlane(const Desktop::eFadeoutPlane plane) {
            return plane == Desktop::FADEOUT_PLANE_WINDOW_TILED || plane == Desktop::FADEOUT_PLANE_WINDOW_FLOATING || plane == Desktop::FADEOUT_PLANE_WINDOW_OVER_FULLSCREEN;
        }

        bool supportedOutput(const PHLMONITOR& monitor) {
            if (!monitor || monitor->m_transform != WL_OUTPUT_TRANSFORM_NORMAL || monitor->isMirror() || !monitor->m_mirrors.empty())
                return false;

            if (monitor->m_cmType != NCMType::CM_SRGB || monitor->inHDR() || monitor->useFP16() || monitor->needsCM() || monitor->needsUnmodifiedCopy())
                return false;

            if (!monitor->m_imageDescription || monitor->m_imageDescription->value().icc.present)
                return false;

            return true;
        }

        bool matchesExpectedRenderSignature(const SFunctionMatch& match) {
            constexpr std::string_view EXPECTED =
                "Render::IHyprRenderer::renderFadeouts(Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, Desktop::eFadeoutPlane, "
                "Hyprutils::Memory::CSharedPointer<CWorkspace>)";
            return match.address && match.demangled == EXPECTED;
        }

        bool matchesExpectedCreateSignature(const SFunctionMatch& match) {
            constexpr std::string_view EXPECTED =
                "Desktop::CWindowFadeout::create(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>, "
                "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>, float)";
            return match.address && match.demangled == EXPECTED;
        }

        bool matchesExpectedSnapshotSignature(const SFunctionMatch& match) {
            constexpr std::string_view EXPECTED =
                "Render::IHyprRenderer::makeSnapshotFB(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>)";
            return match.address && match.demangled == EXPECTED;
        }

        bool finiteBox(const CBox& box) {
            return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) && box.w > 0 && box.h > 0;
        }

        CBox monitorPixelBox(const CBox& geometry, const PHLMONITOR& monitor) {
            CBox box = geometry;
            box.translate(-monitor->m_position).scale(monitor->m_scale);
            return box;
        }

        void includeBox(std::optional<CBox>& bounds, const CBox& box) {
            if (!finiteBox(box))
                return;

            if (!bounds) {
                bounds = box;
                return;
            }

            const double left   = std::min(bounds->x, box.x);
            const double top    = std::min(bounds->y, box.y);
            const double right  = std::max(bounds->x + bounds->w, box.x + box.w);
            const double bottom = std::max(bounds->y + bounds->h, box.y + box.h);
            *bounds             = {left, top, right - left, bottom - top};
        }

        Vector2D windowRenderOffset(const PHLWINDOW& window) {
            Vector2D offset = window->m_floatingOffset;
            if (!window->m_pinned && window->m_workspace)
                offset += window->m_workspace->m_renderOffset->value();
            return offset;
        }

        bool hasUnsupportedWindowContent(const PHLWINDOW& window) {
            if (std::ranges::any_of(window->m_transformers, [](const auto& transformer) {
                    return !transformer || dynamic_cast<Render::CMotionBlurTransformer*>(transformer.get()) == nullptr;
                }))
                return true;

            return std::ranges::any_of(window->m_windowDecorations, [](const auto& decoration) {
                return !decoration || decoration->getDecorationType() == DECORATION_CUSTOM;
            });
        }

        std::optional<CBox> snapshotContentBox(const PHLWINDOW& window, const PHLMONITOR& monitor, const SP<Render::IFramebuffer>& snapshot) {
            if (!window || !monitor || !snapshot || snapshot->m_size.x <= 0 || snapshot->m_size.y <= 0 || hasUnsupportedWindowContent(window))
                return std::nullopt;

            const auto     windowBox     = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
            const auto     renderOffset  = windowRenderOffset(window);
            const Vector2D viewportSize  = {std::max(windowBox.w, 5.0), std::max(windowBox.h, 5.0)};
            const Vector2D renderPosition = windowBox.pos() + renderOffset;
            std::optional<CBox> bounds;

            includeBox(bounds, {renderPosition, viewportSize});

            if (const auto root = window->wlSurface() ? window->wlSurface()->resource() : nullptr; root) {
                const auto reportedSize = window->getReportedSize();
                const bool scaleSubsurfaces = window->sizeAnimation() && window->sizeAnimation()->isBeingAnimated() && reportedSize.x != 0 && reportedSize.y != 0;

                root->breadthfirst(
                    [&](SP<CWLSurfaceResource> surface, const Vector2D& offset, void*) {
                        if (!surface || surface == root || !surface->m_current.texture || surface->m_current.size.x < 1 || surface->m_current.size.y < 1)
                            return;

                        Vector2D size{std::max(surface->m_current.size.x, 2.0), std::max(surface->m_current.size.y, 2.0)};
                        if (scaleSubsurfaces)
                            size = size * (windowBox.size() / reportedSize);

                        if (offset.x + size.x > viewportSize.x)
                            size.x = viewportSize.x - offset.x;
                        if (offset.y + size.y > viewportSize.y)
                            size.y = viewportSize.y - offset.y;

                        includeBox(bounds, {renderPosition + offset, size});
                    },
                    nullptr);
            }

            SBoxExtents stackedExtents;
            SBoxExtents directExtents;
            for (const auto& decoration : window->m_windowDecorations) {
                const auto info      = decoration->getPositioningInfo();
                const auto requested = info.desiredExtents;
                const auto left      = std::max(requested.topLeft.x, 0.0);
                const auto top       = std::max(requested.topLeft.y, 0.0);
                const auto right     = std::max(requested.bottomRight.x, 0.0);
                const auto bottom    = std::max(requested.bottomRight.y, 0.0);

                directExtents.topLeft.x     = std::max(directExtents.topLeft.x, left);
                directExtents.topLeft.y     = std::max(directExtents.topLeft.y, top);
                directExtents.bottomRight.x = std::max(directExtents.bottomRight.x, right);
                directExtents.bottomRight.y = std::max(directExtents.bottomRight.y, bottom);

                if (info.policy != DECORATION_POSITION_STICKY || (decoration->getDecorationFlags() & DECORATION_NON_SOLID))
                    continue;

                if (info.edges & DECORATION_EDGE_LEFT)
                    stackedExtents.topLeft.x += left;
                if (info.edges & DECORATION_EDGE_TOP)
                    stackedExtents.topLeft.y += top;
                if (info.edges & DECORATION_EDGE_RIGHT)
                    stackedExtents.bottomRight.x += right;
                if (info.edges & DECORATION_EDGE_BOTTOM)
                    stackedExtents.bottomRight.y += bottom;
            }

            SBoxExtents decorationExtents{
                .topLeft = {std::max(stackedExtents.topLeft.x, directExtents.topLeft.x), std::max(stackedExtents.topLeft.y, directExtents.topLeft.y)},
                .bottomRight = {std::max(stackedExtents.bottomRight.x, directExtents.bottomRight.x),
                                std::max(stackedExtents.bottomRight.y, directExtents.bottomRight.y)},
            };

            CBox decorationBox = windowBox;
            decorationBox.addExtents(decorationExtents).translate(renderOffset);
            includeBox(bounds, decorationBox);

            const auto xdgSurface = window->m_xdgSurface.lock();
            if (!window->m_isX11 && xdgSurface && window->m_popupHead) {
                const Vector2D popupOrigin = renderPosition - xdgSurface->m_current.geometry.pos();
                window->m_popupHead->breadthfirst(
                    [&](SP<Desktop::View::CPopup> popup, void*) {
                        if (!popup || !popup->m_mapped || popup->inert() || !popup->wlSurface())
                            return;

                        const auto root = popup->wlSurface()->resource();
                        if (!root)
                            return;

                        const Vector2D popupPosition = popupOrigin + popup->coordsRelativeToParent();
                        root->breadthfirst(
                            [&](SP<CWLSurfaceResource> surface, const Vector2D& offset, void*) {
                                if (!surface || !surface->m_current.texture || surface->m_current.size.x < 1 || surface->m_current.size.y < 1)
                                    return;

                                const Vector2D size{std::max(surface->m_current.size.x, 2.0), std::max(surface->m_current.size.y, 2.0)};
                                includeBox(bounds, {popupPosition + offset, size});
                            },
                            nullptr);
                    },
                    nullptr);
            }

            if (!bounds)
                return std::nullopt;

            CBox pixels = monitorPixelBox(*bounds, monitor).intersection(CBox{0, 0, snapshot->m_size.x, snapshot->m_size.y});
            return finiteBox(pixels) ? std::optional<CBox>{pixels} : std::nullopt;
        }

        CBox particleSourceBox(const CBox& sourceContentBox, const Vector2D& textureSize) {
            CBox source = sourceContentBox;
            source.expand(2.0);

            const double left   = std::floor(source.x);
            const double top    = std::floor(source.y);
            const double right  = std::ceil(source.x + source.w);
            const double bottom = std::ceil(source.y + source.h);
            source              = CBox{left, top, right - left, bottom - top};
            return source.intersection(CBox{0, 0, textureSize.x, textureSize.y});
        }

        CBox effectEnvelope(const CBox& sourceBox, const CBox& sourceWindowBox, const CBox& renderBox, const Vector2D& textureSize, const PHLMONITOR& monitor,
                            const float spread, const float turbulence, const float grainSize, const int maxParticles) {
            if (!finiteBox(sourceBox) || !finiteBox(sourceWindowBox) || !finiteBox(renderBox) || textureSize.x <= 0 || textureSize.y <= 0)
                return {};

            const Vector2D renderScale{renderBox.w / textureSize.x, renderBox.h / textureSize.y};
            const Vector2D pivot{renderBox.x + (sourceWindowBox.x + sourceWindowBox.w * 0.5) * renderScale.x,
                                 renderBox.y + (sourceWindowBox.y + sourceWindowBox.h * 0.5) * renderScale.y};
            const std::array<Vector2D, 4> corners = {
                Vector2D{renderBox.x + sourceBox.x * renderScale.x, renderBox.y + sourceBox.y * renderScale.y},
                Vector2D{renderBox.x + (sourceBox.x + sourceBox.w) * renderScale.x, renderBox.y + sourceBox.y * renderScale.y},
                Vector2D{renderBox.x + sourceBox.x * renderScale.x, renderBox.y + (sourceBox.y + sourceBox.h) * renderScale.y},
                Vector2D{renderBox.x + (sourceBox.x + sourceBox.w) * renderScale.x, renderBox.y + (sourceBox.y + sourceBox.h) * renderScale.y},
            };

            double radius = 0.0;
            for (const auto& corner : corners)
                radius = std::max(radius, std::hypot(corner.x - pivot.x, corner.y - pivot.y));

            const double distance = std::max(sourceWindowBox.w, sourceWindowBox.h) * std::clamp(static_cast<double>(spread), 0.0, 0.35);
            const double jitter   = distance * std::clamp(static_cast<double>(turbulence), 0.0, 1.0) * 0.35 * std::sqrt(2.0);
            const double softness = 0.8 * std::max(std::abs(renderScale.x), std::abs(renderScale.y));
            const double budgetSide = std::max(std::floor(std::sqrt(static_cast<double>(std::clamp(maxParticles, 4096, 262144)))) - 1.0, 1.0);
            const double maximumGrain = std::max(std::clamp(static_cast<double>(grainSize), 1.0, 16.0), std::max(sourceBox.w, sourceBox.h) / budgetSide);
            const double maximumScale = std::max(std::abs(renderScale.x), std::abs(renderScale.y));
            // Reserve the AABB growth caused by each grain's local rotation of up to 15 degrees.
            const double rotation = 0.5 * (std::cos(std::numbers::pi / 12.0) + std::sin(std::numbers::pi / 12.0) - 1.0) *
                (maximumGrain + 1.6) * maximumScale;
            const double extent = radius + distance + jitter + softness + rotation + 2.0;

            return CBox{pivot.x - extent, pivot.y - extent, extent * 2.0, extent * 2.0}
                .intersection(CBox{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y})
                .round();
        }

        SFadeoutObservation& observationFor(const SP<Desktop::IFadeout>& fadeout, const SFadeoutCapture& capture) {
            const auto key = fadeoutKey(fadeout);
            auto       it  = g_state.observations.find(key);

            if (it != g_state.observations.end()) {
                const auto locked = it->second.fadeout.lock();
                if (locked && locked.get() == fadeout.get())
                    return it->second;

                g_state.observations.erase(it);
            }

            uint64_t seedBits = key + 0x9E3779B97F4A7C15ULL;
            seedBits          = (seedBits ^ (seedBits >> 30U)) * 0xBF58476D1CE4E5B9ULL;
            seedBits          = (seedBits ^ (seedBits >> 27U)) * 0x94D049BB133111EBULL;
            seedBits ^= seedBits >> 31U;

            auto [inserted, unused] = g_state.observations.emplace(key, SFadeoutObservation{
                                                                           .fadeout           = fadeout,
                                                                           .initialAlpha      = std::max(capture.initialAlpha, 0.001F),
                                                                           .seed              = static_cast<float>(seedBits & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU) * 1024.F,
                                                                           .sourceWindowBox  = capture.sourceWindowBox,
                                                                           .sourceContentBox = capture.sourceContentBox,
                                                                       });
            (void)unused;
            return inserted->second;
        }

        void sweepObservations() {
            std::erase_if(g_state.snapshotCaptures, [](const auto& entry) {
                const auto window   = entry.second.window.lock();
                const auto snapshot = entry.second.snapshot.lock();
                return !window || window.get() != reinterpret_cast<Desktop::View::CWindow*>(entry.first) || !snapshot;
            });

            std::erase_if(g_state.captures, [](const auto& entry) {
                const auto fadeout = entry.second.fadeout.lock();
                return !fadeout || fadeout.get() != reinterpret_cast<Desktop::IFadeout*>(entry.first) || fadeout->done();
            });

            std::erase_if(g_state.observations, [](const auto& entry) {
                const auto fadeout = entry.second.fadeout.lock();
                return !fadeout || fadeout.get() != reinterpret_cast<Desktop::IFadeout*>(entry.first) || fadeout->done();
            });

            if (g_state.observations.empty())
                g_state.observedMonitors.clear();
        }

        SFadeoutObservation* existingObservation(const SP<Desktop::IFadeout>& fadeout) {
            const auto it = g_state.observations.find(fadeoutKey(fadeout));
            if (it == g_state.observations.end())
                return nullptr;

            const auto locked = it->second.fadeout.lock();
            return locked && locked.get() == fadeout.get() ? &it->second : nullptr;
        }

        SFadeoutCapture* capturedFadeout(const SP<Desktop::IFadeout>& fadeout) {
            const auto it = g_state.captures.find(fadeoutKey(fadeout));
            if (it == g_state.captures.end())
                return nullptr;

            const auto locked = it->second.fadeout.lock();
            return locked && locked.get() == fadeout.get() ? &it->second : nullptr;
        }

        void rejectCapturesFor(const PHLMONITOR& monitor, const Desktop::eFadeoutPlane plane, const PHLWORKSPACE& workspace) {
            std::erase_if(g_state.captures, [&](const auto& entry) {
                const auto fadeout = entry.second.fadeout.lock();
                if (!fadeout || fadeout.get() != reinterpret_cast<Desktop::IFadeout*>(entry.first) || fadeout->done())
                    return true;
                if (fadeout->monitor() != monitor || fadeout->plane() != plane)
                    return false;
                return !fadeout->workspace() || fadeout->workspace() == workspace;
            });
        }

        SP<Render::IFramebuffer> makeWindowSnapshotHook(Render::IHyprRenderer* renderer, PHLWINDOW window) {
            const auto original = g_originalSnapshot;
            if (!original)
                return nullptr;

            if (window)
                g_state.snapshotCaptures.erase(fadeoutKey(window));

            auto snapshot = original(renderer, window);
            if (!snapshot || !window)
                return snapshot;

            PHLMONITOR monitor;
            try {
                monitor = window->m_monitor.lock();
                const bool capture = monitor && g_state.initialized.load() && !g_state.unloading.load() && g_state.config.valid() && g_state.config.enabled->value() &&
                    !g_state.circuitBreaker.load() && supportedOutput(monitor);
                if (!capture)
                    return snapshot;

                CBox sourceWindowBox = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
                sourceWindowBox.translate(windowRenderOffset(window));
                sourceWindowBox = monitorPixelBox(sourceWindowBox, monitor);
                const auto sourceContentBox = snapshotContentBox(window, monitor, snapshot);
                if (!finiteBox(sourceWindowBox) || !sourceContentBox)
                    return snapshot;

                g_state.snapshotCaptures.insert_or_assign(fadeoutKey(window), SWindowSnapshotCapture{
                                                                                  .window           = window,
                                                                                  .snapshot         = snapshot,
                                                                                  .sourceWindowBox  = sourceWindowBox,
                                                                                  .sourceContentBox = *sourceContentBox,
                                                                              });
            } catch (...) {
                g_state.snapshotCaptures.erase(fadeoutKey(window));
                activateCircuitBreaker("window snapshot bounds capture failed", monitor);
            }

            return snapshot;
        }

        SP<Desktop::CWindowFadeout> createWindowFadeoutHook(PHLWINDOW window, SP<Render::IFramebuffer> snapshot, float sourceAlpha) {
            const auto original = g_originalCreate;
            if (!original)
                return nullptr;

            std::optional<SWindowSnapshotCapture> capture;
            if (window) {
                const auto key = fadeoutKey(window);
                if (const auto it = g_state.snapshotCaptures.find(key); it != g_state.snapshotCaptures.end()) {
                    const auto capturedWindow   = it->second.window.lock();
                    const auto capturedSnapshot = it->second.snapshot.lock();
                    if (capturedWindow == window && capturedSnapshot == snapshot)
                        capture = it->second;
                    g_state.snapshotCaptures.erase(it);
                }
            }

            auto fadeout = original(window, snapshot, sourceAlpha);
            if (!fadeout || !capture)
                return fadeout;

            try {
                g_state.captures.insert_or_assign(fadeoutKey(fadeout), SFadeoutCapture{
                                                                           .fadeout           = fadeout,
                                                                           .initialAlpha      = sourceAlpha,
                                                                           .sourceWindowBox  = capture->sourceWindowBox,
                                                                           .sourceContentBox = capture->sourceContentBox,
                                                                       });
            } catch (...) {
                activateCircuitBreaker("fadeout capture promotion failed", window ? window->m_monitor.lock() : nullptr);
            }

            return fadeout;
        }

        CTexPassElement::SRenderData nativeTextureData(const SP<Desktop::IFadeout>& fadeout, const SP<Render::ITexture>& texture, const Desktop::SFadeoutRenderEffects& effects,
                                                       const CRegion& damage) {
            CTexPassElement::SRenderData data;
            data.flipEndFrame          = true;
            data.tex                   = texture;
            data.box                   = fadeout->renderBox();
            data.a                     = fadeout->alpha();
            data.damage                = damage;
            data.blur                  = effects.textureBlur.enabled;
            data.blurA                 = effects.textureBlur.alpha;
            data.forceBlurBlend        = effects.textureBlur.forceBlend;
            data.ignoreAlpha           = effects.textureBlur.ignoreAlpha;
            data.blockBlurOptimization = effects.textureBlur.blockBlurOptimization;
            return data;
        }

        void appendNativeEffects(std::vector<UP<IPassElement>>& pending, const PHLMONITOR& monitor, const Desktop::SFadeoutRenderEffects& effects) {
            if (effects.dimAroundAlpha > 0.F) {
                CRectPassElement::SRectData data;
                data.box   = {0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
                data.color = CHyprColor(0, 0, 0, effects.dimAroundAlpha);
                pending.emplace_back(makeUnique<CRectPassElement>(data));
            }

            if (effects.preBlur) {
                float blurAlpha = effects.preBlur->alpha;
                if (blurAlpha <= 0.001F)
                    return;

                CRectPassElement::SRectData data;
                data.box           = effects.preBlur->box;
                data.color         = CHyprColor(0, 0, 0, 0);
                data.blur          = true;
                data.blurA         = blurAlpha;
                data.round         = effects.preBlur->round;
                data.roundingPower = effects.preBlur->roundingPower;
                data.xray          = effects.preBlur->xray;
                pending.emplace_back(makeUnique<CRectPassElement>(data));
            }
        }

        bool mustPassthrough(Render::IHyprRenderer* renderer, const PHLMONITOR& monitor, const Desktop::eFadeoutPlane plane) {
            return !renderer || !g_original || !g_state.initialized.load() || g_state.unloading.load() || !g_state.config.valid() || !g_state.config.enabled->value() ||
                renderer->type() != Render::IHyprRenderer::RT_GL || !Render::GL::g_pHyprOpenGL || g_state.circuitBreaker.load() || !monitor || !isWindowPlane(plane) ||
                !supportedOutput(monitor);
        }
    }

    const std::string& fadeoutHookError() {
        return g_installError;
    }

    bool installFadeoutHook() {
        g_installError.clear();

        auto renderMatches = HyprlandAPI::findFunctionsByName(g_state.handle, "renderFadeouts");
        std::erase_if(renderMatches, [](const auto& match) { return !matchesExpectedRenderSignature(match); });

        if (renderMatches.size() != 1) {
            g_installError = std::format("expected one exact renderFadeouts target, found {}", renderMatches.size());
            Log::logger->log(Log::ERR, "{} expected one exact renderFadeouts target, found {}", LOG_PREFIX, renderMatches.size());
            return false;
        }

        auto snapshotMatches = HyprlandAPI::findFunctionsByName(g_state.handle, "_ZN6Render13IHyprRenderer14makeSnapshotFB");
        std::erase_if(snapshotMatches, [](const auto& match) { return !matchesExpectedSnapshotSignature(match); });

        if (snapshotMatches.size() != 1) {
            g_installError = std::format("expected one exact window makeSnapshotFB target, found {}", snapshotMatches.size());
            Log::logger->log(Log::ERR, "{} expected one exact window makeSnapshotFB target, found {}", LOG_PREFIX, snapshotMatches.size());
            return false;
        }

        // findFunctionsByName searches mangled symbols, so use this class-qualified Itanium prefix before validating the full demangled signature.
        auto createMatches = HyprlandAPI::findFunctionsByName(g_state.handle, "_ZN7Desktop14CWindowFadeout6create");
        std::erase_if(createMatches, [](const auto& match) { return !matchesExpectedCreateSignature(match); });

        if (createMatches.size() != 1) {
            g_installError = std::format("expected one exact CWindowFadeout::create target, found {}", createMatches.size());
            Log::logger->log(Log::ERR, "{} expected one exact CWindowFadeout::create target, found {}", LOG_PREFIX, createMatches.size());
            return false;
        }

        g_state.snapshotHook = HyprlandAPI::createFunctionHook(g_state.handle, snapshotMatches.front().address, reinterpret_cast<const void*>(&makeWindowSnapshotHook));
        if (!g_state.snapshotHook || !g_state.snapshotHook->hook()) {
            if (g_state.snapshotHook)
                HyprlandAPI::removeFunctionHook(g_state.handle, g_state.snapshotHook);
            g_state.snapshotHook = nullptr;
            g_installError       = "failed to activate window makeSnapshotFB hook";
            return false;
        }

        g_originalSnapshot = reinterpret_cast<MakeWindowSnapshotFn>(g_state.snapshotHook->m_original);
        if (!g_originalSnapshot) {
            g_installError = "window makeSnapshotFB hook has no trampoline";
            removeFadeoutHook();
            return false;
        }

        g_state.createHook = HyprlandAPI::createFunctionHook(g_state.handle, createMatches.front().address, reinterpret_cast<const void*>(&createWindowFadeoutHook));
        if (!g_state.createHook || !g_state.createHook->hook()) {
            g_installError = "failed to activate CWindowFadeout::create hook";
            removeFadeoutHook();
            return false;
        }

        g_originalCreate = reinterpret_cast<CreateWindowFadeoutFn>(g_state.createHook->m_original);
        if (!g_originalCreate) {
            g_installError = "CWindowFadeout::create hook has no trampoline";
            removeFadeoutHook();
            return false;
        }

        g_state.renderHook = HyprlandAPI::createFunctionHook(g_state.handle, renderMatches.front().address, reinterpret_cast<const void*>(&renderFadeoutsHook));
        if (!g_state.renderHook || !g_state.renderHook->hook()) {
            g_installError = "failed to activate renderFadeouts hook";
            removeFadeoutHook();
            return false;
        }

        g_original = reinterpret_cast<RenderFadeoutsFn>(g_state.renderHook->m_original);
        if (!g_original) {
            g_installError = "renderFadeouts hook has no trampoline";
            removeFadeoutHook();
            return false;
        }

        return true;
    }

    void removeFadeoutHook() noexcept {
        try {
            if (g_state.renderHook) {
                if (!g_state.renderHook->unhook())
                    Log::logger->log(Log::WARN, "{} renderFadeouts hook was already inactive", LOG_PREFIX);
                HyprlandAPI::removeFunctionHook(g_state.handle, g_state.renderHook);
            }

            if (g_state.createHook) {
                if (!g_state.createHook->unhook())
                    Log::logger->log(Log::WARN, "{} CWindowFadeout::create hook was already inactive", LOG_PREFIX);
                HyprlandAPI::removeFunctionHook(g_state.handle, g_state.createHook);
            }

            if (g_state.snapshotHook) {
                if (!g_state.snapshotHook->unhook())
                    Log::logger->log(Log::WARN, "{} window makeSnapshotFB hook was already inactive", LOG_PREFIX);
                HyprlandAPI::removeFunctionHook(g_state.handle, g_state.snapshotHook);
            }
        } catch (...) {
        }

        g_state.renderHook   = nullptr;
        g_state.snapshotHook = nullptr;
        g_state.createHook   = nullptr;
        g_original           = nullptr;
        g_originalSnapshot   = nullptr;
        g_originalCreate     = nullptr;
    }

    void renderFadeoutsHook(Render::IHyprRenderer* renderer, PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace) noexcept {
        const auto original = g_original;
        if (!original)
            return;

        size_t                              submittedElements = 0;
        std::vector<UP<IPassElement>>       pending;
        const auto submitRemaining = [&]() noexcept {
            for (auto& element : pending) {
                if (!element)
                    continue;
                try {
                    renderer->addPassElement(std::move(element));
                } catch (...) {
                    return;
                }
            }
        };

        try {
            sweepObservations();
        } catch (...) {
        }

        if (mustPassthrough(renderer, monitor, plane)) {
            if (monitor && isWindowPlane(plane)) {
                try {
                    rejectCapturesFor(monitor, plane, workspace);
                } catch (...) {
                }
            }
            original(renderer, monitor, plane, workspace);
            return;
        }

        try {
            std::vector<SP<Desktop::IFadeout>> fadeouts;
            for (const auto& fadeout : Desktop::fadingOutState()->fadeouts()) {
                if (!fadeout || fadeout->monitor() != monitor || fadeout->plane() != plane)
                    continue;
                if (fadeout->workspace() && fadeout->workspace() != workspace)
                    continue;
                fadeouts.emplace_back(fadeout);
            }

            std::ranges::sort(fadeouts, {}, [](const auto& fadeout) { return fadeout->zIndex(); });

            const auto maxActive = static_cast<size_t>(g_state.config.maxActive->value());
            const auto grainSize = g_state.config.grainSize->value();
            const auto spread    = g_state.config.spread->value();
            const auto lateralRepulsion = g_state.config.lateralRepulsion->value();
            const auto directionX = g_state.config.directionX->value();
            const auto directionY = g_state.config.directionY->value();
            const auto turbulence = g_state.config.turbulence->value();
            const auto maxParticles = static_cast<int>(g_state.config.maxParticles->value());

            CRegion                           fakeDamage{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
            pending.reserve(fadeouts.size() * 4);

            for (const auto& fadeout : fadeouts) {
                const auto key = fadeoutKey(fadeout);
                auto* existing = existingObservation(fadeout);
                std::optional<SFadeoutCapture> capture;
                if (!existing) {
                    if (const auto* pendingCapture = capturedFadeout(fadeout); pendingCapture)
                        capture = *pendingCapture;
                    g_state.captures.erase(key);
                }

                const auto framebuffer = fadeout->framebuffer();
                if (!framebuffer)
                    continue;

                const auto texture = framebuffer->getTexture();
                if (!texture || !texture->ok())
                    continue;

                const auto effects = fadeout->effects();
                auto textureData = nativeTextureData(fadeout, texture, effects, fakeDamage);
                const auto windowFadeout = dynamicPointerCast<Desktop::CWindowFadeout>(fadeout);

                const bool eligible = windowFadeout && !effects.textureBlur.enabled &&
                    (existing || (capture && g_state.observations.size() < maxActive));

                if (!eligible) {
                    appendNativeEffects(pending, monitor, effects);
                    pending.emplace_back(makeUnique<CTexPassElement>(std::move(textureData)));
                    continue;
                }

                auto& observation = existing ? *existing : observationFor(fadeout, *capture);
                const float denominator = std::max(observation.initialAlpha, 0.001F);
                const float progress = std::clamp(1.F - fadeout->alpha() / denominator, 0.F, 1.F);
                const CBox sourceBox = particleSourceBox(observation.sourceContentBox, texture->m_size);
                const CBox envelope =
                    effectEnvelope(sourceBox, observation.sourceWindowBox, textureData.box, texture->m_size, monitor, spread, turbulence, grainSize, maxParticles);

                if (!finiteBox(sourceBox) || !finiteBox(envelope)) {
                    g_state.observations.erase(key);
                    appendNativeEffects(pending, monitor, effects);
                    pending.emplace_back(makeUnique<CTexPassElement>(std::move(textureData)));
                    continue;
                }

                appendNativeEffects(pending, monitor, effects);

                SDustPassData dustData{
                    .source            = std::move(textureData),
                    .monitor           = monitor,
                    .sourceBox         = sourceBox,
                    .windowBox         = observation.sourceWindowBox,
                    .effectEnvelope    = envelope,
                    .progress          = progress,
                    .initialAlpha      = observation.initialAlpha,
                    .seed              = observation.seed,
                    .grainSize         = grainSize,
                    .spread            = spread,
                    .lateralRepulsion  = lateralRepulsion,
                    .directionX        = directionX,
                    .directionY        = directionY,
                    .turbulence        = turbulence,
                    .maxParticles      = maxParticles,
                };

                pending.emplace_back(makeUnique<CThanosDustPassElement>(std::move(dustData)));
                g_state.observedMonitors.emplace(monitor);
            }

            for (auto& element : pending) {
                renderer->addPassElement(std::move(element));
                ++submittedElements;
            }

            sweepObservations();
        } catch (const std::exception& error) {
            activateCircuitBreaker("fadeout preparation exception", monitor);
            Log::logger->log(Log::ERR, "{} renderFadeouts preparation failed: {}", LOG_PREFIX, error.what());
            if (submittedElements == 0)
                original(renderer, monitor, plane, workspace);
            else
                submitRemaining();
        } catch (...) {
            activateCircuitBreaker("unknown fadeout preparation exception", monitor);
            Log::logger->log(Log::ERR, "{} renderFadeouts preparation failed with an unknown exception", LOG_PREFIX);
            if (submittedElements == 0)
                original(renderer, monitor, plane, workspace);
            else
                submitRemaining();
        }
    }

}
