#pragma once

#include <hyprland/src/debug/log/Logger.hpp>

#include <format>
#include <string_view>
#include <utility>

namespace HyprThanos::Compat {

    template <typename... Args>
    void log(Hyprutils::CLI::eLogLevel level, std::format_string<Args...> format, Args&&... args) {
        // The variadic logger gained a location parameter. The two-argument,
        // already-formatted overload has the same meaning in both APIs.
        const auto message = std::format(format, std::forward<Args>(args)...);
        Log::logger->log(level, std::string_view{message});
    }

}
