#pragma once

#include <hyprland/src/render/Shader.hpp>

#include <array>

namespace HyprThanos {

    enum class EShaderState : uint8_t {
        UNINITIALIZED,
        READY,
        FAILED,
    };

    class CThanosDustShader {
      public:
        CThanosDustShader() = default;
        ~CThanosDustShader() = default;

        CThanosDustShader(const CThanosDustShader&)            = delete;
        CThanosDustShader& operator=(const CThanosDustShader&) = delete;

        bool                     ensureReady() noexcept;
        void                     destroy() noexcept;
        EShaderState             state() const;
        SP<CShader>              shader() const;

        GLint                    progressLocation() const;
        GLint                    seedLocation() const;
        GLint                    grainSizeLocation() const;
        GLint                    spreadLocation() const;
        GLint                    lateralRepulsionLocation() const;
        GLint                    directionLocation() const;
        GLint                    turbulenceLocation() const;
        GLint                    sourceBoxLocation() const;
        GLint                    windowBoxLocation() const;
        GLint                    textureSizeLocation() const;
        GLint                    drawSizeLocation() const;
        GLint                    gridSizeLocation() const;

      private:
        enum ECustomUniform : size_t {
            U_PROGRESS,
            U_SEED,
            U_GRAIN_SIZE,
            U_SPREAD,
            U_LATERAL_REPULSION,
            U_DIRECTION,
            U_TURBULENCE,
            U_SOURCE_BOX,
            U_WINDOW_BOX,
            U_TEXTURE_SIZE,
            U_DRAW_SIZE,
            U_GRID_SIZE,
            U_COUNT,
        };

        EShaderState               m_state = EShaderState::UNINITIALIZED;
        SP<CShader>                m_shader;
        std::array<GLint, U_COUNT> m_uniforms = {};
    };

}
