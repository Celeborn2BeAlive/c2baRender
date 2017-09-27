#include "rendering/integrators/AOIntegrator.hpp"
#include <iostream>
namespace c2ba
{

void AOIntegrator::doPreprocess()
{
    m_RandomGenerators.resize(m_nTileCount);
    for (size_t tileId = 0; tileId < m_nTileCount; ++tileId) {
        m_RandomGenerators[tileId].seed(tileId * 1024u);
    }
    m_Rays.resize((m_AORaySqrtCount * m_AORaySqrtCount * m_nTileSize * m_nTileSize + m_nTileSize * m_nTileSize) * m_nThreadCount, Ray{});
    m_AORays.resize(m_nTileSize * m_nTileSize * m_nThreadCount);
}

void AOIntegrator::doRender(const RenderTileParams & params)
{
    //renderSingleRayAPI(params);
    //renderStreamRayAPI(params);
    renderStreamRaySOAAPI(params);
}

Ray AOIntegrator::primaryRay(size_t pixelId, float2 uPixel, const RenderTileParams & params) const
{
    const auto pixelCoords = pixelImageCoords<float2>(pixelId, params);
    const auto rasterPos = pixelCoords + uPixel;
    const auto ndcPos = float2(-1.f) + 2.f * rasterPos * m_RcpFramebufferSize;
    const auto viewSpacePos = divideW<float4>(m_RcpProjMatrix * float4(ndcPos, -1.f, 1.f));
    const auto worldSpacePos = divideW<float3>(m_RcpViewMatrix * viewSpacePos);
    const auto viewOrigin = float3(m_RcpViewMatrix[3]);

    return Ray{ viewOrigin, worldSpacePos - viewOrigin };
}

void AOIntegrator::renderSingleRayAPI(const RenderTileParams & params)
{
    const auto aoRayCount = m_AORaySqrtCount * m_AORaySqrtCount;

    std::uniform_real_distribution<float> d{ 0, 1 };
    auto & g = m_RandomGenerators[params.tileId];

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId)
    {
        auto ray = primaryRay(pixelId, float2(d(g), d(g)), params);
        if (m_Scene->intersect(ray))
        {
            float3 N;
            m_Scene->evalHitPoint(ray, Normal(N));

            float3 Tx, Ty;
            makeOrthonormals(N, Tx, Ty);

            float visibility = 0.f;
            for (size_t aoRayIdx = 0; aoRayIdx < aoRayCount; ++aoRayIdx)
            {
                const float3 localDir = sampleHemisphereCosine(d(g), d(g));
                const float3 worldDir = localDir.x * Tx + localDir.y * Ty + localDir.z * N;

                Ray aoRay{ hitPoint(ray), worldDir, 0.01f, 100.f };
                if (!m_Scene->occluded(aoRay))
                    visibility += 1.f;
            }

            params.outBuffer[pixelId] += float4(float3(visibility / aoRayCount), 1);
        }
        else
            params.outBuffer[pixelId] += float4(float3(0), 1);
    }
}

void AOIntegrator::renderStreamRayAPI(const RenderTileParams & params)
{
    const auto aoRayCount = m_AORaySqrtCount * m_AORaySqrtCount;
    auto * rays = m_Rays.data() + params.threadId * (m_nTileSize * m_nTileSize + m_nTileSize * m_nTileSize * aoRayCount);

    std::uniform_real_distribution<float> d{ 0, 1 };

    auto & g = m_RandomGenerators[params.tileId];

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId) {
        rays[pixelId] = primaryRay(pixelId, float2(d(g), d(g)), params);
    }

    m_Scene->intersect(rays, pixelCount(params), RayProperties::Coherent);

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId)
    {
        auto & ray = rays[pixelId];
        auto * aoRays = rays + m_nTileSize * m_nTileSize + pixelId * aoRayCount;

        if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
        {
            float3 N;
            m_Scene->evalHitPoint(ray, Normal(N));

            float3 Tx, Ty;
            makeOrthonormals(N, Tx, Ty);

            for (size_t aoRayIdx = 0; aoRayIdx < aoRayCount; ++aoRayIdx)
            {
                const float3 localDir = sampleHemisphereCosine(d(g), d(g));
                const float3 worldDir = localDir.x * Tx + localDir.y * Ty + localDir.z * N;

                aoRays[aoRayIdx] = Ray{ hitPoint(ray), worldDir, 0.01f, 100.f };
            }
        }
        else
        {
            for (size_t aoRayIdx = 0; aoRayIdx < aoRayCount; ++aoRayIdx)
            {
                auto & aoRay = aoRays[aoRayIdx];
                aoRay.tnear = 1.f;
                aoRay.tfar = 0.f;
            }
        }
    }

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId)
    {
        auto * aoRays = rays + m_nTileSize * m_nTileSize + pixelId * aoRayCount;
        m_Scene->occluded(aoRays, aoRayCount, RayProperties::Coherent);
    }

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId)
    {
        auto * aoRays = rays + m_nTileSize * m_nTileSize + pixelId * aoRayCount;
        float visibility = 0.f;
        if (rays[pixelId].geomID != RTC_INVALID_GEOMETRY_ID)
        {
            for (size_t aoRayIdx = 0; aoRayIdx < aoRayCount; ++aoRayIdx)
            {
                if (aoRays[aoRayIdx].geomID != 0)
                    visibility += 1.f;
            }
        }

        params.outBuffer[pixelId] += float4(float3(visibility / aoRayCount), 1);
    }
}

void AOIntegrator::renderStreamRaySOAAPI(const RenderTileParams & params)
{
    const auto aoRayCount = m_AORaySqrtCount * m_AORaySqrtCount;
    auto * rays = m_Rays.data() + params.threadId * (m_nTileSize * m_nTileSize + m_nTileSize * m_nTileSize * aoRayCount);
    auto * aoRays = m_AORays.data() + params.threadId * m_nTileSize * m_nTileSize;

    std::uniform_real_distribution<float> d{ 0, 1 };

    auto & g = m_RandomGenerators[params.tileId];

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId) {
        rays[pixelId] = primaryRay(pixelId, float2(d(g), d(g)), params);
    }

    m_Scene->intersect(rays, pixelCount(params), RayProperties::Coherent);

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId)
    {
        memset(&aoRays[pixelId], 0, sizeof(aoRays[pixelId]));
        std::fill(aoRays[pixelId].tnear, aoRays[pixelId].tnear + m_AORayCount, 1.f);
        std::fill(aoRays[pixelId].mask, aoRays[pixelId].mask + m_AORayCount, 0xFFFFFFFF);
        std::fill(aoRays[pixelId].geomID, aoRays[pixelId].geomID + m_AORayCount, Ray::InvalidID);
        std::fill(aoRays[pixelId].instID, aoRays[pixelId].instID + m_AORayCount, Ray::InvalidID);
        std::fill(aoRays[pixelId].primID, aoRays[pixelId].primID + m_AORayCount, Ray::InvalidID);
    }

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId)
    {
        auto & ray = rays[pixelId];

        if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
        {
            std::fill(aoRays[pixelId].tnear, aoRays[pixelId].tnear + m_AORayCount, 0.01f);
            std::fill(aoRays[pixelId].tfar, aoRays[pixelId].tfar + m_AORayCount, 100.f);

            const auto aoOrg = hitPoint(ray);
            std::fill(aoRays[pixelId].orgx, aoRays[pixelId].orgx + m_AORayCount, aoOrg.x);
            std::fill(aoRays[pixelId].orgy, aoRays[pixelId].orgy + m_AORayCount, aoOrg.y);
            std::fill(aoRays[pixelId].orgz, aoRays[pixelId].orgz + m_AORayCount, aoOrg.z);

            float3 N;
            m_Scene->evalHitPoint(ray, Normal(N));
            float3 Tx, Ty;
            makeOrthonormals(N, Tx, Ty);

            for (size_t aoRayIdx = 0; aoRayIdx < aoRayCount; ++aoRayIdx)
            {
                const float3 localDir = sampleHemisphereCosine(d(g), d(g));
                const float3 worldDir = localDir.x * Tx + localDir.y * Ty + localDir.z * N;

                aoRays[pixelId].dirx[aoRayIdx] = worldDir.x;
                aoRays[pixelId].diry[aoRayIdx] = worldDir.y;
                aoRays[pixelId].dirz[aoRayIdx] = worldDir.z;
            }
        }
    }

    m_Scene->occluded(&aoRays[0], pixelCount(params), RayProperties::Coherent);

    //RaySOAPtrs aoSOAPtrs = raySOAPtrs(aoRays[0]);
    //for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId)
    //{
    //    m_Scene->occluded(aoSOAPtrs, aoRayCount, RayProperties::Coherent);
    //    advance(aoSOAPtrs, sizeof(AORayPacket));
    //}

    for (size_t pixelId = 0, count = pixelCount(params); pixelId < count; ++pixelId)
    {
        float visibility = 0.f;
        if (rays[pixelId].geomID != RTC_INVALID_GEOMETRY_ID)
        {
            for (size_t aoRayIdx = 0; aoRayIdx < aoRayCount; ++aoRayIdx)
            {
                if (aoRays[pixelId].geomID[aoRayIdx] != 0)
                    visibility += 1.f;
            }
        }

        params.outBuffer[pixelId] += float4(float3(visibility / aoRayCount), 1);
    }
}

}