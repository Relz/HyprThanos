#include "dust_shader.hpp"

#include "shaders.hpp"
#include "state.hpp"

#include <hyprland/src/debug/log/Logger.hpp>

#include <string>

namespace HyprThanos {

    bool CThanosDustShader::ensureReady() noexcept {
        if (m_state == EShaderState::READY)
            return true;
        if (m_state == EShaderState::FAILED)
            return false;

        try {
            m_uniforms.fill(-1);
            m_shader = makeShared<CShader>();

            if (!m_shader->createProgram(std::string{Shaders::VERTEX}, std::string{Shaders::FRAGMENT}, true, true)) {
                Log::logger->log(Log::ERR, "{} dust shader compilation or linking failed", LOG_PREFIX);
                m_shader.reset();
                m_state = EShaderState::FAILED;
                return false;
            }

            const auto program = m_shader->program();
            m_uniforms[U_PROGRESS]           = glGetUniformLocation(program, "u_progress");
            m_uniforms[U_SEED]               = glGetUniformLocation(program, "u_seed");
            m_uniforms[U_GRAIN_SIZE]         = glGetUniformLocation(program, "u_grain_size");
            m_uniforms[U_SPREAD]             = glGetUniformLocation(program, "u_spread");
            m_uniforms[U_LATERAL_REPULSION]  = glGetUniformLocation(program, "u_lateral_repulsion");
            m_uniforms[U_DIRECTION]          = glGetUniformLocation(program, "u_direction");
            m_uniforms[U_TURBULENCE]         = glGetUniformLocation(program, "u_turbulence");
            m_uniforms[U_SOURCE_BOX]          = glGetUniformLocation(program, "u_source_box");
            m_uniforms[U_WINDOW_BOX]          = glGetUniformLocation(program, "u_window_box");
            m_uniforms[U_TEXTURE_SIZE]        = glGetUniformLocation(program, "u_texture_size");
            m_uniforms[U_DRAW_SIZE]           = glGetUniformLocation(program, "u_draw_size");
            m_uniforms[U_GRID_SIZE]           = glGetUniformLocation(program, "u_grid_size");

            const bool standardLocationsValid = m_shader->getUniformLocation(SHADER_PROJ) >= 0 && m_shader->getUniformLocation(SHADER_TEX) >= 0 &&
                m_shader->getUniformLocation(SHADER_ALPHA) >= 0 && m_shader->getUniformLocation(SHADER_SHADER_VAO) > 0 && m_shader->getUniformLocation(SHADER_SHADER_VBO) > 0;

            const bool customLocationsValid = m_uniforms[U_PROGRESS] >= 0 && m_uniforms[U_SEED] >= 0 && m_uniforms[U_GRAIN_SIZE] >= 0 && m_uniforms[U_SPREAD] >= 0 &&
                m_uniforms[U_LATERAL_REPULSION] >= 0 && m_uniforms[U_DIRECTION] >= 0 && m_uniforms[U_TURBULENCE] >= 0 &&
                m_uniforms[U_SOURCE_BOX] >= 0 && m_uniforms[U_WINDOW_BOX] >= 0 && m_uniforms[U_TEXTURE_SIZE] >= 0 &&
                m_uniforms[U_DRAW_SIZE] >= 0 && m_uniforms[U_GRID_SIZE] >= 0;

            if (!standardLocationsValid || !customLocationsValid || glGetError() != GL_NO_ERROR) {
                Log::logger->log(Log::ERR, "{} dust shader has invalid resources or uniforms", LOG_PREFIX);
                m_shader->destroy();
                m_shader.reset();
                m_state = EShaderState::FAILED;
                return false;
            }

            m_state = EShaderState::READY;
            Log::logger->log(Log::INFO, "{} dust shader ready", LOG_PREFIX);
            return true;
        } catch (...) {
            if (m_shader)
                m_shader->destroy();
            m_shader.reset();
            m_state = EShaderState::FAILED;
            Log::logger->log(Log::ERR, "{} exception while preparing the dust shader", LOG_PREFIX);
            return false;
        }
    }

    void CThanosDustShader::destroy() noexcept {
        try {
            if (m_shader)
                m_shader->destroy();
        } catch (...) {
        }

        m_shader.reset();
        m_uniforms.fill(-1);
        m_state = EShaderState::UNINITIALIZED;
    }

    EShaderState CThanosDustShader::state() const {
        return m_state;
    }

    SP<CShader> CThanosDustShader::shader() const {
        return m_shader;
    }

    GLint CThanosDustShader::progressLocation() const {
        return m_uniforms[U_PROGRESS];
    }

    GLint CThanosDustShader::seedLocation() const {
        return m_uniforms[U_SEED];
    }

    GLint CThanosDustShader::grainSizeLocation() const {
        return m_uniforms[U_GRAIN_SIZE];
    }

    GLint CThanosDustShader::spreadLocation() const {
        return m_uniforms[U_SPREAD];
    }

    GLint CThanosDustShader::lateralRepulsionLocation() const {
        return m_uniforms[U_LATERAL_REPULSION];
    }

    GLint CThanosDustShader::directionLocation() const {
        return m_uniforms[U_DIRECTION];
    }

    GLint CThanosDustShader::turbulenceLocation() const {
        return m_uniforms[U_TURBULENCE];
    }

    GLint CThanosDustShader::sourceBoxLocation() const {
        return m_uniforms[U_SOURCE_BOX];
    }

    GLint CThanosDustShader::windowBoxLocation() const {
        return m_uniforms[U_WINDOW_BOX];
    }

    GLint CThanosDustShader::textureSizeLocation() const {
        return m_uniforms[U_TEXTURE_SIZE];
    }

    GLint CThanosDustShader::drawSizeLocation() const {
        return m_uniforms[U_DRAW_SIZE];
    }

    GLint CThanosDustShader::gridSizeLocation() const {
        return m_uniforms[U_GRID_SIZE];
    }

}
