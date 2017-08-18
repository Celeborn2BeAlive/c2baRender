#include "rendering/integrators/GeometryIntegrator.hpp"

namespace c2ba
{

void GeometryIntegrator::doRender(const RenderTileParams & params) const
{
    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;
            const float2 rasterPos = float2(params.beginX + pixelX + 0.5f, params.beginY + pixelY + 0.5f);
            renderNg(rasterPos, params.outBuffer + pixelId);
        }
    }
}

void GeometryIntegrator::renderNg(const float2 & rasterPos, float4 * pixelPtr) const
{
    const float2 ndcPos = float2(-1) + 2.f * float2(rasterPos / float2(m_nFramebufferWidth, m_nFramebufferHeight));

    float4 viewSpacePos = m_RcpProjMatrix * float4(ndcPos, -1.f, 1.f);
    viewSpacePos /= viewSpacePos.w;

    float4 worldSpacePos = m_RcpViewMatrix * viewSpacePos;
    worldSpacePos /= worldSpacePos.w;

    RTCRay ray;
    ray.org[0] = m_RcpViewMatrix[3][0];
    ray.org[1] = m_RcpViewMatrix[3][1];
    ray.org[2] = m_RcpViewMatrix[3][2];

    ray.dir[0] = worldSpacePos[0] - ray.org[0];
    ray.dir[1] = worldSpacePos[1] - ray.org[1];
    ray.dir[2] = worldSpacePos[2] - ray.org[2];

    ray.tnear = 0.f;
    ray.tfar = std::numeric_limits<float>::infinity();
    ray.instID = RTC_INVALID_GEOMETRY_ID;
    ray.geomID = RTC_INVALID_GEOMETRY_ID;
    ray.primID = RTC_INVALID_GEOMETRY_ID;
    ray.mask = 0xFFFFFFFF;
    ray.time = 0.0f;

    rtcIntersect(m_Scene->m_rtcScene, ray);

    if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
        *pixelPtr += float4(glm::abs(float3(ray.Ng[0], ray.Ng[1], ray.Ng[2])), 1);
    else
        *pixelPtr += float4(float3(0), 1);
}

}