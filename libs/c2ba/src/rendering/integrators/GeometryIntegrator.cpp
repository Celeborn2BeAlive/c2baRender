#include "rendering/integrators/GeometryIntegrator.hpp"

namespace c2ba
{

void GeometryIntegrator::doPreprocess()
{
    m_RandomGenerators.resize(m_nTileCount);
    for (size_t tileId = 0; tileId < m_nTileCount; ++tileId) {
        m_RandomGenerators[tileId].seed(tileId * 1024u);
    }
}

void GeometryIntegrator::doRender(const RenderTileParams & params)
{
    std::uniform_real_distribution<float> d{ 0, 1 };
    auto & g = m_RandomGenerators[params.tileId];

    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;
            float4 * pixelPtr = params.outBuffer + pixelId;

            const auto rasterPos = float2(params.beginX + pixelX + d(g), params.beginY + pixelY + d(g));
            const auto ndcPos = float2(-1) + 2.f * float2(rasterPos / float2(m_nFramebufferWidth, m_nFramebufferHeight));
            const auto viewSpacePos = divideW<float4>(m_RcpProjMatrix * float4(ndcPos, -1.f, 1.f));
            const auto worldSpacePos = divideW<float3>(m_RcpViewMatrix * viewSpacePos);
            const auto viewOrigin = float3(m_RcpViewMatrix[3]);

            Ray ray{ viewOrigin, worldSpacePos - viewOrigin };

            if (m_Scene->intersect(ray))
            {
                float3 value;
                Facing facing;
                m_Scene->evalHitPoint(ray, Normal(value), TriangleFacing(facing));

                *pixelPtr += float4(facing == Facing::Back ? float3(0, 1, 0) : float3(1, 0, 1), 1);
            }
            else {
                *pixelPtr += float4(float3(0), 1);
            }
        }
    }
}

}