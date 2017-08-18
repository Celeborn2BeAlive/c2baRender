#include "rendering/integrators/AOIntegrator.hpp"

namespace c2ba
{

void AOIntegrator::doPreprocess()
{
    m_RandomGenerators.resize(m_nTileCount);
    for (size_t tileId = 0; tileId < m_nTileCount; ++tileId) {
        m_RandomGenerators[tileId].seed(tileId * 1024u);
    }
}

void AOIntegrator::doRender(const RenderTileParams & params) const
{
    std::uniform_real_distribution<float> d{ 0, 1 };

    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;
            const float2 rasterPos = float2(params.beginX + pixelX + 0.5f, params.beginY + pixelY + 0.5f);
            renderAO(rasterPos, params.outBuffer + pixelId, m_RandomGenerators[params.tileId], d);
        }
    }
}

void AOIntegrator::renderAO(const float2 & rasterPos, float4 * pixelPtr, std::mt19937 & g, const std::uniform_real_distribution<float> & d) const
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
    {
        const auto Ng = faceForward(normalize(float3(ray.Ng[0], ray.Ng[1], ray.Ng[2])), -float3(ray.dir[0], ray.dir[1], ray.dir[2]));
        float3 Tx, Ty;
        makeOrthonormals(Ng, Tx, Ty);

        const size_t aoRaySqrtCount = 1;
        const size_t aoRayCount = aoRaySqrtCount * aoRaySqrtCount;
        const float delta = 1.f / aoRayCount;

        float visibility = 0.f;
        for (size_t j = 0; j < aoRaySqrtCount; ++j)
        {
            const float u2 = d(g);
            for (size_t i = 0; i < aoRaySqrtCount; ++i)
            {
                const float u1 = d(g);
                const float3 localDir = sampleHemisphereCosine(u1, u2);
                const float3 worldDir = localDir.x * Tx + localDir.y * Ty + localDir.z * Ng;

                RTCRay aoRay;
                aoRay.org[0] = ray.org[0] + ray.tfar * ray.dir[0] + 0.01 * Ng[0];
                aoRay.org[1] = ray.org[1] + ray.tfar * ray.dir[1] + 0.01 * Ng[1];
                aoRay.org[2] = ray.org[2] + ray.tfar * ray.dir[2] + 0.01 * Ng[2];

                aoRay.dir[0] = worldDir[0];
                aoRay.dir[1] = worldDir[1];
                aoRay.dir[2] = worldDir[2];

                aoRay.tnear = 0.f;
                aoRay.tfar = 100.f;
                aoRay.instID = RTC_INVALID_GEOMETRY_ID;
                aoRay.geomID = RTC_INVALID_GEOMETRY_ID;
                aoRay.primID = RTC_INVALID_GEOMETRY_ID;
                aoRay.mask = 0xFFFFFFFF;
                aoRay.time = 0.0f;

                rtcIntersect(m_Scene->m_rtcScene, aoRay);

                if (aoRay.geomID == RTC_INVALID_GEOMETRY_ID)
                    visibility += 1.f;
            }
        }
        visibility *= delta;

        *pixelPtr += float4(float3(visibility), 1);

    }
    else
        *pixelPtr += float4(float3(0), 1);
}

}