#pragma once

#include "dust_shader.hpp"

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Render {
    class IFramebuffer;
}

namespace HyprThanos {

    inline constexpr const char* PLUGIN_NAME    = "hyprthanos";
    inline constexpr const char* LOG_PREFIX     = "[hyprthanos]";
    inline constexpr const char* DUST_PASS_NAME = "CThanosDustPassElement";

    struct SConfig {
        SP<Config::Values::CBoolValue>  enabled;
        SP<Config::Values::CFloatValue> grainSize;
        SP<Config::Values::CFloatValue> spread;
        SP<Config::Values::CFloatValue> lateralRepulsion;
        SP<Config::Values::CFloatValue> directionX;
        SP<Config::Values::CFloatValue> directionY;
        SP<Config::Values::CFloatValue> turbulence;
        SP<Config::Values::CIntValue>   maxActive;
        SP<Config::Values::CIntValue>   maxParticles;

        bool valid() const {
            return enabled && grainSize && spread && lateralRepulsion && directionX && directionY && turbulence && maxActive && maxParticles;
        }

        void reset() {
            enabled.reset();
            grainSize.reset();
            spread.reset();
            lateralRepulsion.reset();
            directionX.reset();
            directionY.reset();
            turbulence.reset();
            maxActive.reset();
            maxParticles.reset();
        }
    };

    struct SFadeoutObservation {
        WP<Desktop::IFadeout> fadeout;
        float                 initialAlpha = 1.F;
        float                 seed         = 0.F;
        CBox                  sourceWindowBox;
        CBox                  sourceContentBox;
    };

    struct SFadeoutCapture {
        WP<Desktop::IFadeout> fadeout;
        float                 initialAlpha = 1.F;
        CBox                  sourceWindowBox;
        CBox                  sourceContentBox;
    };

    struct SWindowSnapshotCapture {
        PHLWINDOWREF             window;
        WP<Render::IFramebuffer> snapshot;
        CBox                     sourceWindowBox;
        CBox                     sourceContentBox;
    };

    struct SPluginState {
        HANDLE                                                handle = nullptr;
        CFunctionHook*                                        renderHook = nullptr;
        CFunctionHook*                                        snapshotHook = nullptr;
        CFunctionHook*                                        createHook = nullptr;
        SConfig                                               config;
        std::unique_ptr<CThanosDustShader>                    shader;
        std::unordered_map<uintptr_t, SWindowSnapshotCapture> snapshotCaptures;
        std::unordered_map<uintptr_t, SFadeoutCapture>        captures;
        std::unordered_map<uintptr_t, SFadeoutObservation>    observations;
        std::unordered_set<PHLMONITORREF>                     observedMonitors;
        std::atomic_bool                                      initialized    = false;
        std::atomic_bool                                      unloading      = false;
        std::atomic_bool                                      circuitBreaker = false;
        bool                                                  circuitLogged  = false;
    };

    inline SPluginState g_state;

    template <typename T>
    inline uintptr_t fadeoutKey(const SP<T>& fadeout) {
        return reinterpret_cast<uintptr_t>(fadeout.get());
    }

    void activateCircuitBreaker(const char* reason, PHLMONITOR monitor = nullptr) noexcept;

}
