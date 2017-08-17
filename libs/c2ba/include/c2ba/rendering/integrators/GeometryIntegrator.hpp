#pragma once

#include "Integrator.hpp"

namespace c2ba
{

class GeometryIntegrator: public Integrator
{
public:
    void render(size_t startSample, size_t sampleCount, size_t startX, size_t startY, size_t countX, size_t countY, float4 * outBuffer) const override;
private:
    void renderNg(const float2 & rasterPos, float4 * pixelPtr) const;
};

}