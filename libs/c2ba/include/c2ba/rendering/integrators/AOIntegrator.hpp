#pragma once

#include <random>
#include <vector>

#include "Integrator.hpp"

namespace c2ba
{

class AOIntegrator : public Integrator
{
    void doPreprocess() override;

    void doRender(const RenderTileParams & params) override;

    void renderSingleRayAPI(const RenderTileParams & params);

    void renderStreamRayAPI(const RenderTileParams & params);

    void renderStreamRaySOAAPI(const RenderTileParams & params);

    Ray primaryRay(size_t pixelId, float2 uPixel, const RenderTileParams & params) const;

    std::vector<std::mt19937> m_RandomGenerators;
    std::vector<Ray> m_Rays;

    static const size_t m_AORaySqrtCount = 4;
    static const size_t m_AORayCount = m_AORaySqrtCount * m_AORaySqrtCount;

    using AORayPacket = RaySOA<m_AORayCount>;
    std::vector<AORayPacket> m_AORays;
};

}