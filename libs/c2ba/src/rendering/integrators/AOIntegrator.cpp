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

    m_Rays.resize((m_AORaySqrtCount * m_AORaySqrtCount * m_nTileSize * m_nTileSize + m_nTileSize * m_nTileSize) * m_nThreadCount, RTCRay{});
    m_AORayPackets.resize(m_nTileSize * m_nTileSize * m_nThreadCount, AORayPacket{});
}

void AOIntegrator::doRender(const RenderTileParams & params)
{
    renderNormalSingleRayAPI(params);
    //renderStreamSingleRayAPI(params);
    //renderStreamSOARayAPI(params);
}

void AOIntegrator::renderNormalSingleRayAPI(const RenderTileParams & params)
{
    std::uniform_real_distribution<float> d{ 0, 1 };
    auto & g = m_RandomGenerators[params.tileId];

    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;
            const float2 rasterPos = float2(params.beginX + pixelX + 0.5f, params.beginY + pixelY + 0.5f);

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

                const size_t aoRayCount = m_AORaySqrtCount * m_AORaySqrtCount;
                const float delta = 1.f / aoRayCount;

                float visibility = 0.f;
                for (size_t j = 0; j < m_AORaySqrtCount; ++j)
                {
                    for (size_t i = 0; i < m_AORaySqrtCount; ++i)
                    {
                        const float u2 = d(g);
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

                params.outBuffer[pixelId] += float4(float3(visibility), 1);

            }
            else
                params.outBuffer[pixelId] += float4(float3(0), 1);
        }
    }
}

void AOIntegrator::renderStreamSingleRayAPI(const RenderTileParams & params)
{
    const size_t aoRayCount = m_AORaySqrtCount * m_AORaySqrtCount;
    const float delta = 1.f / aoRayCount;

    RTCRay * rays = m_Rays.data() + params.threadId * (m_nTileSize * m_nTileSize + m_nTileSize * m_nTileSize * aoRayCount);

    std::uniform_real_distribution<float> d{ 0, 1 };

    auto & g = m_RandomGenerators[params.tileId];

    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;
            const float2 rasterPos = float2(params.beginX + pixelX + 0.5f, params.beginY + pixelY + 0.5f);

            const float2 ndcPos = float2(-1) + 2.f * float2(rasterPos / float2(m_nFramebufferWidth, m_nFramebufferHeight));

            float4 viewSpacePos = m_RcpProjMatrix * float4(ndcPos, -1.f, 1.f);
            viewSpacePos /= viewSpacePos.w;

            float4 worldSpacePos = m_RcpViewMatrix * viewSpacePos;
            worldSpacePos /= worldSpacePos.w;

            RTCRay & ray = rays[pixelId];

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
        }
    }

    RTCIntersectContext context;
    context.flags = RTC_INTERSECT_COHERENT;
    context.userRayExt = nullptr;

    rtcIntersectNM(m_Scene->m_rtcScene, &context, (RTCRayN *)rays, 1, params.countX * params.countY, sizeof(RTCRay));

    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;

            RTCRay & ray = rays[pixelId];
            RTCRay * aoRays = rays + m_nTileSize * m_nTileSize + pixelId * aoRayCount;

            if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
            {
                for (size_t j = 0; j < m_AORaySqrtCount; ++j)
                {
                    for (size_t i = 0; i < m_AORaySqrtCount; ++i)
                    {
                        auto & aoRay = aoRays[i + j * m_AORaySqrtCount];

                        const auto Ng = faceForward(normalize(float3(ray.Ng[0], ray.Ng[1], ray.Ng[2])), -float3(ray.dir[0], ray.dir[1], ray.dir[2]));
                        float3 Tx, Ty;
                        makeOrthonormals(Ng, Tx, Ty);

                        const float u2 = d(g);
                        const float u1 = d(g);
                        const float3 localDir = sampleHemisphereCosine(u1, u2);
                        const float3 worldDir = localDir.x * Tx + localDir.y * Ty + localDir.z * Ng;

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
                    }
                }
            }
            else
            {
                for (size_t j = 0; j < m_AORaySqrtCount; ++j)
                {
                    for (size_t i = 0; i < m_AORaySqrtCount; ++i)
                    {
                        auto & aoRay = aoRays[i + j * m_AORaySqrtCount];
                        aoRay.tnear = 1.f;
                        aoRay.tfar = 0.f;
                    }
                }
            }
        }
    }

    rtcIntersectNM(m_Scene->m_rtcScene, &context, (RTCRayN *)(rays + m_nTileSize * m_nTileSize), 1, aoRayCount * params.countX * params.countY, sizeof(RTCRay));

    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;

            const RTCRay & ray = rays[pixelId];
            RTCRay * aoRays = rays + m_nTileSize * m_nTileSize + pixelId * aoRayCount;

            float visibility = 0.f;

            if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
            {
                for (size_t j = 0; j < m_AORaySqrtCount; ++j)
                {
                    for (size_t i = 0; i < m_AORaySqrtCount; ++i)
                    {
                        auto & aoRay = aoRays[i + j * m_AORaySqrtCount];
                        if (aoRay.geomID == RTC_INVALID_GEOMETRY_ID)
                            visibility += 1.f;
                    }
                }
            }
            else
                visibility = 1.f;

            const float delta = 1.f / aoRayCount;
            visibility *= delta;
            params.outBuffer[pixelId] += float4(float3(visibility), 1);
        }
    }
}

void AOIntegrator::renderStreamSOARayAPI(const RenderTileParams & params)
{
    const size_t aoRayCount = m_AORaySqrtCount * m_AORaySqrtCount;
    const float delta = 1.f / aoRayCount;

    RTCRay * rays = m_Rays.data() + params.threadId * (m_nTileSize * m_nTileSize + m_nTileSize * m_nTileSize * aoRayCount);
    AORayPacket * aoRays = m_AORayPackets.data() + params.threadId * m_nTileSize * m_nTileSize;

    std::uniform_real_distribution<float> d{ 0, 1 };

    auto & g = m_RandomGenerators[params.tileId];

    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;
            const float2 rasterPos = float2(params.beginX + pixelX + 0.5f, params.beginY + pixelY + 0.5f);

            const float2 ndcPos = float2(-1) + 2.f * float2(rasterPos / float2(m_nFramebufferWidth, m_nFramebufferHeight));

            float4 viewSpacePos = m_RcpProjMatrix * float4(ndcPos, -1.f, 1.f);
            viewSpacePos /= viewSpacePos.w;

            float4 worldSpacePos = m_RcpViewMatrix * viewSpacePos;
            worldSpacePos /= worldSpacePos.w;

            RTCRay & ray = rays[pixelId];

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
        }
    }

    RTCIntersectContext context;
    context.flags = RTC_INTERSECT_COHERENT;
    context.userRayExt = nullptr;

    rtcIntersectNM(m_Scene->m_rtcScene, &context, (RTCRayN *)rays, 1, params.countX * params.countY, sizeof(RTCRay));

    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;

            RTCRay & ray = rays[pixelId];
            AORayPacket & aoRayPacket = aoRays[pixelId];

            if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
            {
                for (size_t j = 0; j < m_AORaySqrtCount; ++j)
                {
                    for (size_t i = 0; i < m_AORaySqrtCount; ++i)
                    {
                        auto aoRayIdx = i + j * m_AORaySqrtCount;

                        const auto Ng = faceForward(normalize(float3(ray.Ng[0], ray.Ng[1], ray.Ng[2])), -float3(ray.dir[0], ray.dir[1], ray.dir[2]));
                        float3 Tx, Ty;
                        makeOrthonormals(Ng, Tx, Ty);

                        const float u2 = d(g);
                        const float u1 = d(g);
                        const float3 localDir = sampleHemisphereCosine(u1, u2);
                        const float3 worldDir = localDir.x * Tx + localDir.y * Ty + localDir.z * Ng;

                        aoRayPacket.orgx[aoRayIdx] = ray.org[0] + ray.tfar * ray.dir[0] + 0.01 * Ng[0];
                        aoRayPacket.orgy[aoRayIdx] = ray.org[1] + ray.tfar * ray.dir[1] + 0.01 * Ng[1];
                        aoRayPacket.orgz[aoRayIdx] = ray.org[2] + ray.tfar * ray.dir[2] + 0.01 * Ng[2];

                        aoRayPacket.dirx[aoRayIdx] = worldDir[0];
                        aoRayPacket.diry[aoRayIdx] = worldDir[1];
                        aoRayPacket.dirz[aoRayIdx] = worldDir[2];

                        aoRayPacket.tnear[aoRayIdx] = 0.f;
                        aoRayPacket.tfar[aoRayIdx] = 100.f;
                        aoRayPacket.instID[aoRayIdx] = RTC_INVALID_GEOMETRY_ID;
                        aoRayPacket.geomID[aoRayIdx] = RTC_INVALID_GEOMETRY_ID;
                        aoRayPacket.primID[aoRayIdx] = RTC_INVALID_GEOMETRY_ID;
                        aoRayPacket.mask[aoRayIdx] = 0xFFFFFFFF;
                        aoRayPacket.time[aoRayIdx] = 0.0f;
                    }
                }
            }
            else
            {
                for (size_t j = 0; j < m_AORaySqrtCount; ++j)
                {
                    for (size_t i = 0; i < m_AORaySqrtCount; ++i)
                    {
                        auto aoRayIdx = i + j * m_AORaySqrtCount;

                        aoRayPacket.tnear[aoRayIdx] = 1.f;
                        aoRayPacket.tfar[aoRayIdx] = 0.f;
                    }
                }
            }
        }
    }

    // With normal ray mode:
    //int32_t valid[m_AORayCount];
    //std::fill(valid, valid + m_AORayCount, 0xFFFFFFFF);

    //for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    //{
    //    for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
    //    {
    //        const size_t pixelId = pixelX + pixelY * params.countX;
    //        RTCRay & ray = rays[pixelId];
    //        AORayPacket & aoRayPacket = aoRays[pixelId];

    //        if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
    //            rtcIntersect4(valid, m_Scene->m_rtcScene, aoRayPacket);
    //    }
    //}

    // With stream ray mode:
    rtcOccludedNM(m_Scene->m_rtcScene, &context, aoRays, m_AORayCount, params.countX * params.countY, sizeof(AORayPacket));
    
    for (size_t pixelY = 0; pixelY < params.countY; ++pixelY)
    {
        for (size_t pixelX = 0; pixelX < params.countX; ++pixelX)
        {
            const size_t pixelId = pixelX + pixelY * params.countX;

            const RTCRay & ray = rays[pixelId];
            AORayPacket & aoRayPacket = aoRays[pixelId];

            float visibility = 0.f;

            if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
            {
                for (size_t j = 0; j < m_AORaySqrtCount; ++j)
                {
                    for (size_t i = 0; i < m_AORaySqrtCount; ++i)
                    {
                        auto aoRayIdx = i + j * m_AORaySqrtCount;
                        if (aoRayPacket.geomID[aoRayIdx])
                            visibility += 1.f;
                    }
                }
            }
            else
                visibility = 1.f;

            const float delta = 1.f / aoRayCount;
            visibility *= delta;
            params.outBuffer[pixelId] += float4(float3(visibility), 1);
        }
    }
}

}