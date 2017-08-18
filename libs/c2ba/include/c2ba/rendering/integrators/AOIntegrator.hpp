#pragma once

#include <random>
#include <vector>

#include "Integrator.hpp"

namespace c2ba
{

class AOIntegrator : public Integrator
{
    void doPreprocess() override;

    void doRender(const RenderTileParams & params) const override;

    void renderAO(const float2 & rasterPos, float4 * pixelPtr, std::mt19937 & g, const std::uniform_real_distribution<float> & d) const;

    mutable std::vector<std::mt19937> m_RandomGenerators;
};

}