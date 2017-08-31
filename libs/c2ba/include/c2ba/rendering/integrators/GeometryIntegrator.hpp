#pragma once

#include "Integrator.hpp"

namespace c2ba
{

class GeometryIntegrator : public Integrator
{
    void doRender(const RenderTileParams & params) override;

    void renderNg(const float2 & rasterPos, float4 * pixelPtr) const;
};

}