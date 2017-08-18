#pragma once

#include "Integrator.hpp"

namespace c2ba
{

class GeometryIntegrator : public Integrator
{
public:
    void doRender(const RenderTileParams & params) const override;
private:
    void renderNg(const float2 & rasterPos, float4 * pixelPtr) const;
};

}