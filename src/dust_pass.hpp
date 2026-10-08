#pragma once

#include "state.hpp"
#include "hyprland_compat.hpp"

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

namespace HyprThanos {

    struct SDustPassData {
        CTexPassElement::SRenderData source;
        PHLMONITORREF                monitor;
        CBox                         sourceBox;
        CBox                         windowBox;
        CBox                         effectEnvelope;
        float                        progress          = 0.F;
        float                        initialAlpha      = 1.F;
        float                        seed              = 0.F;
        float                        grainSize         = 3.F;
        float                        spread            = 0.12F;
        float                        lateralRepulsion = 0.35F;
        float                        directionX        = 1.F;
        float                        directionY        = -0.35F;
        float                        turbulence        = 0.5F;
        int                          maxParticles      = 131072;
    };

    class CThanosDustPassElement final : public IPassElement {
      public:
        explicit CThanosDustPassElement(SDustPassData data);
        ~CThanosDustPassElement() override = default;

#if HYPRTHANOS_RENDER_CONTEXT
        std::vector<UP<IPassElement>> draw(Render::CRenderContext& context) override;
        bool                          needsLiveBlur(Render::CRenderContext& context) override;
        bool                          needsPrecomputeBlur(Render::CRenderContext& context) override;
        std::optional<CBox>           boundingBox(Render::CRenderContext& context) override;
        CRegion                       opaqueRegion(Render::CRenderContext& context) override;
#else
        std::vector<UP<IPassElement>> draw() override;
        bool                          needsLiveBlur() override;
        bool                          needsPrecomputeBlur() override;
        std::optional<CBox>           boundingBox() override;
        CRegion                       opaqueRegion() override;
#endif
        const char*                   passName() override;
        ePassElementType              type() override;

      private:
        std::vector<UP<IPassElement>> fallback() const;
        bool drawDust(Compat::RenderContext& context, PHLMONITOR monitor);

        SDustPassData m_data;
    };

}
