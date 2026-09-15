#pragma once

#include <array>
#include <string_view>

namespace HyprThanos::Compat {

    struct SHookContract {
        std::string_view name;
        std::string_view search;
        std::string_view signature;

        template <typename Match>
        bool matches(const Match& match) const {
            return match.address && match.demangled == signature;
        }
    };

    inline constexpr SHookContract RENDER_FADEOUTS{
        "renderFadeouts", "renderFadeouts",
        "Render::IHyprRenderer::renderFadeouts(Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, Desktop::eFadeoutPlane, "
#if __has_include(<hyprland/src/workspace/HLWorkspace.hpp>)
        "Hyprutils::Memory::CSharedPointer<Workspace::CHLWorkspace>)",
#else
        "Hyprutils::Memory::CSharedPointer<CWorkspace>)",
#endif
    };

    // findFunctionsByName searches mangled symbols. Use class-qualified Itanium prefixes to narrow overloads,
    // then validate the complete demangled signature before installing a hook.
    inline constexpr SHookContract WINDOW_SNAPSHOT{
        "window makeSnapshotFB", "_ZN6Render13IHyprRenderer14makeSnapshotFB",
        "Render::IHyprRenderer::makeSnapshotFB(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>)",
    };

    inline constexpr SHookContract CREATE_FADEOUT{
        "CWindowFadeout::create", "_ZN7Desktop14CWindowFadeout6create",
        "Desktop::CWindowFadeout::create(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>, "
        "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>, float)",
    };

    inline constexpr std::array HOOK_CONTRACTS{RENDER_FADEOUTS, WINDOW_SNAPSHOT, CREATE_FADEOUT};

}
