#include "dust_shader.hpp"
#include "fadeout_hook.hpp"
#include "state.hpp"

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/version.h>

#include <stdexcept>
#include <string>

namespace HyprThanos {

    namespace {
        template <typename T>
        void registerConfigValue(const SP<T>& value) {
            if (!value || !HyprlandAPI::addConfigValueV2(g_state.handle, value))
                throw std::runtime_error("failed to register typed configuration");
        }

        void registerConfig() {
            using namespace Config::Values;

            g_state.config.enabled = makeShared<CBoolValue>("plugin:hyprthanos:enabled", "Enable the Thanos close animation", false);
            g_state.config.grainSize = makeShared<CFloatValue>("plugin:hyprthanos:grain_size", "Dust grain size in physical pixels", 3.F,
                                                               SFloatValueOptions{.min = 1.F, .max = 16.F});
            g_state.config.spread = makeShared<CFloatValue>("plugin:hyprthanos:spread", "Maximum particle displacement relative to the window", 0.12F,
                                                             SFloatValueOptions{.min = 0.F, .max = 0.35F});
            g_state.config.lateralRepulsion = makeShared<CFloatValue>("plugin:hyprthanos:lateral_repulsion", "Lateral dust separation across the travel direction", 0.35F,
                                                                      SFloatValueOptions{.min = 0.F, .max = 1.F});
            g_state.config.directionX = makeShared<CFloatValue>("plugin:hyprthanos:direction_x", "Horizontal dust direction", 0.5F,
                                                                 SFloatValueOptions{.min = -1.F, .max = 1.F});
            g_state.config.directionY = makeShared<CFloatValue>("plugin:hyprthanos:direction_y", "Vertical dust direction", -0.35F,
                                                                SFloatValueOptions{.min = -1.F, .max = 1.F});
            g_state.config.turbulence = makeShared<CFloatValue>("plugin:hyprthanos:turbulence", "Particle direction variation", 0.5F,
                                                                SFloatValueOptions{.min = 0.F, .max = 1.F});
            g_state.config.maxActive = makeShared<CIntValue>("plugin:hyprthanos:max_active", "Maximum simultaneous dust fadeouts", 8,
                                                              SIntValueOptions{.min = 1, .max = 32});
            g_state.config.maxParticles = makeShared<CIntValue>("plugin:hyprthanos:max_particles", "Maximum particle instances per fadeout", 131072,
                                                                 SIntValueOptions{.min = 4096, .max = 262144});

            registerConfigValue(g_state.config.enabled);
            registerConfigValue(g_state.config.grainSize);
            registerConfigValue(g_state.config.spread);
            registerConfigValue(g_state.config.lateralRepulsion);
            registerConfigValue(g_state.config.directionX);
            registerConfigValue(g_state.config.directionY);
            registerConfigValue(g_state.config.turbulence);
            registerConfigValue(g_state.config.maxActive);
            registerConfigValue(g_state.config.maxParticles);
        }

        [[noreturn]] void rejectLoad(const std::string& message) {
            if (g_state.handle)
                HyprlandAPI::addNotification(g_state.handle, std::string(LOG_PREFIX) + " " + message, CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 7000.F);
            throw std::runtime_error(std::string(LOG_PREFIX) + " " + message);
        }
    }

    void activateCircuitBreaker(const char* reason, PHLMONITOR monitor) noexcept {
        const bool wasActive = g_state.circuitBreaker.exchange(true);

        try {
            if (!wasActive) {
                g_state.circuitLogged = true;
                Log::logger->log(Log::ERR, "{} circuit breaker activated: {}", LOG_PREFIX, reason ? reason : "unknown error");
            }

            if (monitor && g_pHyprRenderer)
                g_pHyprRenderer->damageMonitor(monitor);
        } catch (...) {
        }
    }

}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    using namespace HyprThanos;

    g_state.config.reset();
    g_state.shader.reset();
    g_state.snapshotCaptures.clear();
    g_state.captures.clear();
    g_state.observations.clear();
    g_state.observedMonitors.clear();
    g_state.handle = handle;
    g_state.unloading.store(false);
    g_state.circuitBreaker.store(false);
    g_state.circuitLogged = false;

    const std::string serverABI = __hyprland_api_get_hash();
    const std::string clientABI = __hyprland_api_get_client_hash();

    if (serverABI != clientABI)
        rejectLoad("ABI mismatch; rebuild against the running Hyprland (server " + serverABI + ", plugin " + clientABI + ")");

#if !defined(__x86_64__)
    rejectLoad("the Hyprland function-hook implementation is supported only on x86_64");
#endif

    if (!g_pHyprRenderer)
        rejectLoad("renderer is not initialized");

    if (g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL || !Render::GL::g_pHyprOpenGL)
        rejectLoad("only the OpenGL renderer is supported");

    try {
        registerConfig();
        g_state.shader = std::make_unique<CThanosDustShader>();

        if (!installFadeoutHook())
            rejectLoad(fadeoutHookError().empty() ? "failed to install the exact fadeout hooks" : fadeoutHookError());
    } catch (...) {
        removeFadeoutHook();
        g_state.shader.reset();
        g_state.config.reset();
        throw;
    }

    g_state.initialized.store(true);

    if (!HyprlandAPI::reloadConfig()) {
        removeFadeoutHook();
        g_state.initialized.store(false);
        rejectLoad("failed to queue a configuration reload");
    }

    Log::logger->log(Log::INFO, "{} loaded for Hyprland commit {} with ABI {}", LOG_PREFIX, GIT_COMMIT_HASH, serverABI);

    return {PLUGIN_NAME, "Thanos-style window close effect for Hyprland", "Relz", HYPRTHANOS_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    using namespace HyprThanos;

    Log::logger->log(Log::INFO, "{} unload begin", LOG_PREFIX);

    g_state.unloading.store(true);
    g_state.initialized.store(false);
    removeFadeoutHook();

    if (g_pHyprRenderer) {
        g_pHyprRenderer->currentPass().removeAllOfType(DUST_PASS_NAME);
        g_pHyprRenderer->m_renderPass.removeAllOfType(DUST_PASS_NAME);

        for (const auto& monitorRef : g_state.observedMonitors) {
            if (const auto monitor = monitorRef.lock(); monitor)
                g_pHyprRenderer->damageMonitor(monitor);
        }
    }

    g_state.snapshotCaptures.clear();
    g_state.captures.clear();
    g_state.observations.clear();
    g_state.observedMonitors.clear();

    if (Render::GL::g_pHyprOpenGL) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();

        if (g_state.shader && g_state.shader->state() == EShaderState::READY) {
            if (const auto coreShader = Render::GL::g_pHyprOpenGL->getShaderVariant(Render::SH_FRAG_PASSTHRURGBA); coreShader)
                Render::GL::g_pHyprOpenGL->useShader(coreShader);
        }
    }

    if (g_state.shader)
        g_state.shader->destroy();

    g_state.shader.reset();
    g_state.config.reset();
    g_state.circuitBreaker.store(false);
    g_state.circuitLogged = false;
    g_state.handle        = nullptr;

    Log::logger->log(Log::INFO, "{} unload complete", LOG_PREFIX);
}
