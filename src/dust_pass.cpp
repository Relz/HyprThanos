#include "dust_pass.hpp"

#include "dust_shader.hpp"

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>

namespace HyprThanos {

    namespace {
        void clearGLErrors() {
            while (glGetError() != GL_NO_ERROR) {
            }
        }

        bool finiteBox(const CBox& box) {
            return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h) && box.w > 0 && box.h > 0;
        }

        std::array<GLfloat, 4> boxUniform(const CBox& box) {
            return {static_cast<GLfloat>(box.x), static_cast<GLfloat>(box.y), static_cast<GLfloat>(box.w), static_cast<GLfloat>(box.h)};
        }

        struct SParticleGrid {
            GLfloat grainSize = 1.F;
            GLint   columns   = 1;
            GLint   rows      = 1;
            GLsizei instances = 1;
        };

        std::optional<SParticleGrid> particleGrid(const CBox& sourceBox, const float requestedGrainSize, const int requestedBudget) {
            if (!finiteBox(sourceBox))
                return std::nullopt;

            const auto budget = static_cast<uint64_t>(std::clamp(requestedBudget, 4096, 262144));
            auto       countFor = [&](const double grainSize) {
                const auto columns = static_cast<uint64_t>(std::max(std::ceil(sourceBox.w / grainSize), 1.0));
                const auto rows    = static_cast<uint64_t>(std::max(std::ceil(sourceBox.h / grainSize), 1.0));
                return std::array<uint64_t, 3>{columns, rows, columns * rows};
            };

            double grainSize  = std::max(static_cast<double>(requestedGrainSize), 1.0);
            auto   dimensions = countFor(grainSize);

            if (dimensions[2] > budget) {
                double low  = grainSize;
                double high = std::max({sourceBox.w, sourceBox.h, grainSize});

                for (int iteration = 0; iteration < 32; ++iteration) {
                    const double middle = (low + high) * 0.5;
                    if (countFor(middle)[2] > budget)
                        low = middle;
                    else
                        high = middle;
                }

                grainSize = std::nextafter(high, std::numeric_limits<double>::infinity());
            }

            auto effectiveGrainSize = static_cast<GLfloat>(grainSize);
            if (static_cast<double>(effectiveGrainSize) < grainSize)
                effectiveGrainSize = std::nextafter(effectiveGrainSize, std::numeric_limits<GLfloat>::infinity());

            dimensions = countFor(static_cast<double>(effectiveGrainSize));
            while (dimensions[2] > budget) {
                effectiveGrainSize = std::nextafter(effectiveGrainSize, std::numeric_limits<GLfloat>::infinity());
                dimensions         = countFor(static_cast<double>(effectiveGrainSize));
            }

            if (dimensions[0] > static_cast<uint64_t>(std::numeric_limits<GLint>::max()) ||
                dimensions[1] > static_cast<uint64_t>(std::numeric_limits<GLint>::max()) || dimensions[2] == 0 ||
                dimensions[2] > static_cast<uint64_t>(std::numeric_limits<GLsizei>::max()))
                return std::nullopt;

            return SParticleGrid{
                .grainSize = effectiveGrainSize,
                .columns   = static_cast<GLint>(dimensions[0]),
                .rows      = static_cast<GLint>(dimensions[1]),
                .instances = static_cast<GLsizei>(dimensions[2]),
            };
        }
    }

    CThanosDustPassElement::CThanosDustPassElement(SDustPassData data) : m_data(std::move(data)) {
    }

    std::vector<UP<IPassElement>> CThanosDustPassElement::fallback() const {
        std::vector<UP<IPassElement>> elements;
        elements.emplace_back(makeUnique<CTexPassElement>(m_data.source));
        return elements;
    }

    std::vector<UP<IPassElement>> CThanosDustPassElement::draw() {
        const auto monitor = m_data.monitor.lock();
        try {
            if (!monitor || g_state.unloading.load() || g_state.circuitBreaker.load() || !g_state.shader || !m_data.source.tex || !m_data.source.tex->ok())
                return fallback();

            if (!g_state.shader->ensureReady()) {
                activateCircuitBreaker("dust shader is unavailable", monitor);
                return fallback();
            }

            if (!drawDust(monitor)) {
                activateCircuitBreaker("dust pre-draw validation failed", monitor);
                return fallback();
            }

            return {};
        } catch (...) {
            activateCircuitBreaker("dust pre-draw exception", monitor);
            return fallback();
        }
    }

    bool CThanosDustPassElement::drawDust(PHLMONITOR monitor) {
        if (!monitor || !g_pHyprRenderer || !Render::GL::g_pHyprOpenGL || !g_state.shader || m_data.source.damage.empty())
            return false;

        const auto texture = m_data.source.tex;
        const auto shader  = g_state.shader->shader();
        if (!texture || !texture->ok() || !shader || shader->program() == 0 || !finiteBox(m_data.source.box) || !finiteBox(m_data.sourceBox) ||
            !finiteBox(m_data.windowBox) || texture->m_size.x <= 0 || texture->m_size.y <= 0)
            return false;

        const auto grid = particleGrid(m_data.sourceBox, m_data.grainSize, m_data.maxParticles);
        if (!grid)
            return false;

        CRegion drawRegion = m_data.source.damage.copy();
        CRegion envelope{m_data.effectEnvelope};
        g_pHyprRenderer->m_renderData.renderModif.applyToRegion(envelope);
        drawRegion.intersect(envelope);

        if (!g_pHyprRenderer->m_renderData.damage.empty())
            drawRegion.intersect(g_pHyprRenderer->m_renderData.damage);

        if (!g_pHyprRenderer->m_renderData.clipBox.empty())
            drawRegion.intersect(g_pHyprRenderer->m_renderData.clipBox);

        if (!m_data.source.clipRegion.empty())
            drawRegion.intersect(m_data.source.clipRegion);

        if (drawRegion.empty())
            return true;

        clearGLErrors();

        CBox drawBox = m_data.source.box;
        g_pHyprRenderer->m_renderData.renderModif.applyToBox(drawBox);

        const auto monitorInverse = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
        auto       transform      = texture->m_transform;
        if (g_pHyprRenderer->monitorTransformEnabled())
            transform = Math::composeTransform(monitorInverse, transform);

        const auto projection = g_pHyprRenderer->projectBoxToTarget(drawBox, transform);
        const auto sourceBox  = boxUniform(m_data.sourceBox);
        const auto windowBox  = boxUniform(m_data.windowBox);

        if (glGetError() != GL_NO_ERROR)
            return false;

        glActiveTexture(GL_TEXTURE0);
        texture->bind();

        if (glGetError() != GL_NO_ERROR) {
            texture->unbind();
            return false;
        }

        texture->setTexParameter(GL_TEXTURE_WRAP_S, m_data.source.wrapX == WRAP_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        texture->setTexParameter(GL_TEXTURE_WRAP_T, m_data.source.wrapY == WRAP_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE);

        if (g_pHyprRenderer->m_renderData.useNearestNeighbor) {
            texture->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            texture->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        } else {
            texture->setTexParameter(GL_TEXTURE_MAG_FILTER, texture->magFilter);
            texture->setTexParameter(GL_TEXTURE_MIN_FILTER, texture->minFilter);
        }

        if (glGetError() != GL_NO_ERROR) {
            texture->unbind();
            return false;
        }

        Render::GL::g_pHyprOpenGL->useShader(shader);

        shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, projection.getMatrix());
        shader->setUniformInt(SHADER_TEX, 0);
        shader->setUniformFloat(SHADER_ALPHA, std::clamp(m_data.initialAlpha, 0.F, 1.F));

        glUniform1f(g_state.shader->progressLocation(), std::clamp(m_data.progress, 0.F, 1.F));
        glUniform1f(g_state.shader->seedLocation(), m_data.seed);
        glUniform1f(g_state.shader->grainSizeLocation(), grid->grainSize);
        glUniform1f(g_state.shader->spreadLocation(), std::clamp(m_data.spread, 0.F, 0.35F));
        glUniform1f(g_state.shader->lateralRepulsionLocation(), std::clamp(m_data.lateralRepulsion, 0.F, 1.F));
        glUniform2f(g_state.shader->directionLocation(), std::clamp(m_data.directionX, -1.F, 1.F), std::clamp(m_data.directionY, -1.F, 1.F));
        glUniform1f(g_state.shader->turbulenceLocation(), std::clamp(m_data.turbulence, 0.F, 1.F));
        glUniform4fv(g_state.shader->sourceBoxLocation(), 1, sourceBox.data());
        glUniform4fv(g_state.shader->windowBoxLocation(), 1, windowBox.data());
        glUniform2f(g_state.shader->textureSizeLocation(), static_cast<GLfloat>(texture->m_size.x), static_cast<GLfloat>(texture->m_size.y));
        glUniform2f(g_state.shader->drawSizeLocation(), static_cast<GLfloat>(drawBox.w), static_cast<GLfloat>(drawBox.h));
        glUniform2i(g_state.shader->gridSizeLocation(), grid->columns, grid->rows);

        const auto vao = static_cast<GLuint>(shader->getUniformLocation(SHADER_SHADER_VAO));
        if (vao == 0 || glGetError() != GL_NO_ERROR) {
            if (const auto coreShader = Render::GL::g_pHyprOpenGL->getShaderVariant(Render::SH_FRAG_PASSTHRURGBA); coreShader)
                Render::GL::g_pHyprOpenGL->useShader(coreShader);
            glActiveTexture(GL_TEXTURE0);
            texture->unbind();
            Render::GL::g_pHyprOpenGL->scissor(nullptr);
            return false;
        }

        Render::GL::g_pHyprOpenGL->blend(true);
        glBindVertexArray(vao);

        bool drew = false;
        drawRegion.forEachRect([&](const auto& rect) {
            Render::GL::g_pHyprOpenGL->scissor(&rect, g_pHyprRenderer->m_renderData.transformDamage);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, grid->instances);
            drew = true;
        });

        const GLenum error = glGetError();

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        texture->unbind();
        Render::GL::g_pHyprOpenGL->scissor(nullptr);

        if (error != GL_NO_ERROR) {
            activateCircuitBreaker("OpenGL error after dust draw", monitor);
            return true;
        }

        return drew;
    }

    bool CThanosDustPassElement::needsLiveBlur() {
        return false;
    }

    bool CThanosDustPassElement::needsPrecomputeBlur() {
        return false;
    }

    const char* CThanosDustPassElement::passName() {
        return DUST_PASS_NAME;
    }

    ePassElementType CThanosDustPassElement::type() {
        return EK_CUSTOM;
    }

    std::optional<CBox> CThanosDustPassElement::boundingBox() {
        const auto monitor = m_data.monitor.lock();
        if (!monitor || monitor->m_scale <= 0.F)
            return std::nullopt;
        return m_data.effectEnvelope.copy().scale(1.F / monitor->m_scale).round();
    }

    CRegion CThanosDustPassElement::opaqueRegion() {
        return {};
    }

}
