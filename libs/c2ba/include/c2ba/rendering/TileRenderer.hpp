#pragma once

#include <thread>
#include <vector>
#include <random>
#include <numeric>
#include <atomic>

#include "../scene/Scene.hpp"
#include "TiledFramebuffer.hpp"

namespace c2ba
{

class TileRenderer
{
public:
    TileRenderer()
    {
        const auto threadCount = std::thread::hardware_concurrency() > 1u ? std::thread::hardware_concurrency() - 1u : 1u; // Try to keep one thread for the main loop
        for (size_t threadId = 0; threadId < threadCount; ++threadId) {
            m_ThreadPool.emplace_back([this, threadId]()
            {
                renderTask(threadId);
            });
        }
    }

    ~TileRenderer()
    {
        m_bDone = true;
        for (auto & thread : m_ThreadPool) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void setScene(const RTScene & scene)
    {
        m_Scene = &scene;
    }

    void setFramebuffer(size_t fbWidth, size_t fbHeight)
    {
        m_Framebuffer = TiledFramebuffer(s_TileSize, fbWidth, fbHeight);
        m_Image.resize(fbWidth * fbHeight);

        m_TilePermutation.resize(m_Framebuffer.tileCount());
        std::iota(begin(m_TilePermutation), end(m_TilePermutation), 0u);

        std::random_device rd;
        std::mt19937 g{ rd() };

        std::shuffle(begin(m_TilePermutation), end(m_TilePermutation), g);

        m_Dirty = true;
    }

    void setProjMatrix(const float4x4 & projMatrix)
    {
        m_RcpProjMatrix = inverse(projMatrix);
        m_Dirty = true;
    }

    void setViewMatrix(const float4x4 & viewMatrix)
    {
        m_RcpViewMatrix = inverse(viewMatrix);
        m_Dirty = true;
    }

    void clear()
    {
        fill(begin(m_Image), end(m_Image), float4(0));
        m_Framebuffer.clear();
        m_Dirty = false;
        m_NextTile = 0;
    }

    void render()
    {
        if (m_Dirty) {
            m_bRunning = false;
            clear();
        }

        m_bRunning = true;

        m_Framebuffer.copy(m_Image.data());
    }

    void stop()
    {
        m_bDone = true;
        for (auto & t : m_ThreadPool) {
            t.join();
        }
    }

    const float4 * getPixels() const
    {
        return m_Image.data();
    }

private:
    bool m_bRunning = false;
    bool m_bDone = false;

    static const size_t s_TileSize = 16;
    TiledFramebuffer m_Framebuffer;

    void renderRasterPos(const float2 & rasterPos, float4 * pixelPtr)
    {
        *pixelPtr += float4(float2(rasterPos / float2(m_Framebuffer.imageWidth(), m_Framebuffer.imageHeight())), 0.f, 1.f);
    }

    void renderNg(const float2 & rasterPos, float4 * pixelPtr)
    {
        const float2 ndcPos = float2(-1) + 2.f * float2(rasterPos / float2(m_Framebuffer.imageWidth(), m_Framebuffer.imageHeight()));

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

    void renderAO(const float2 & rasterPos, float4 * pixelPtr, std::mt19937 & g, const std::uniform_real_distribution<float> & d)
    {
        const float2 ndcPos = float2(-1) + 2.f * float2(rasterPos / float2(m_Framebuffer.imageWidth(), m_Framebuffer.imageHeight()));

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

    void renderTask(size_t threadId)
    {
        std::mt19937 g{ (unsigned int)threadId };
        std::uniform_real_distribution<float> d;

        while (!m_bDone)
        {
            while (!m_bRunning && !m_bDone) {
                std::this_thread::sleep_for(std::chrono::milliseconds(256));
            }
            if (m_bDone) {
                break;
            }

            const auto tileId = m_TilePermutation[m_NextTile++ % m_Framebuffer.tileCount()];
            const auto l = m_Framebuffer.lockTile(tileId);

            const auto bounds = m_Framebuffer.tileBounds(tileId);
            float4 * tilePtr = m_Framebuffer.tileDataPtr(tileId);

            for (size_t pixelY = 0; pixelY < bounds.countY; ++pixelY)
            {
                for (size_t pixelX = 0; pixelX < bounds.countX; ++pixelX)
                {
                    const size_t pixelId = pixelX + pixelY * s_TileSize;

                    const float2 rasterPos = float2(bounds.beginX + pixelX + 0.5f, bounds.beginY + pixelY + 0.5f);
                    const float2 ndcPos = float2(-1) + 2.f * float2(rasterPos / float2(m_Framebuffer.imageWidth(), m_Framebuffer.imageHeight()));

                    //renderRasterPos(rasterPos, tilePtr + pixelId);
                    //renderNg(rasterPos, tilePtr + pixelId);
                    renderAO(rasterPos, tilePtr + pixelId, g, d);
                }
            }

            if (m_bDone) {
                break;
            }
        }
    }

    bool m_Dirty = true;

    std::vector<size_t> m_TilePermutation;

    std::vector<std::thread> m_ThreadPool;
    std::vector<float4> m_Image;

    const RTScene * m_Scene = nullptr;

    float4x4 m_RcpProjMatrix; // Screen to Cam
    float4x4 m_RcpViewMatrix; // Cam to World

    std::atomic_uint32_t m_NextTile{ 0 };
};

}