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

    void renderNormalSingleRayAPI(const RenderTileParams & params);

    void renderStreamSingleRayAPI(const RenderTileParams & params);

    void renderStreamSOARayAPI(const RenderTileParams & params);

    std::vector<std::mt19937> m_RandomGenerators;
    std::vector<RTCRay> m_Rays;

    static const size_t m_AORaySqrtCount = 4;
    static const size_t m_AORayCount = m_AORaySqrtCount * m_AORaySqrtCount;

    using AORayPacket = RTCRayNt<m_AORayCount>;
    std::vector<AORayPacket> m_AORayPackets;
};

}