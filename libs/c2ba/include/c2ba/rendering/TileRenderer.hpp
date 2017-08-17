#pragma once

#include <thread>
#include <vector>
#include <random>
#include <numeric>
#include <atomic>

#include "c2ba/scene/Scene.hpp"
#include "c2ba/threads.hpp"
#include "TiledFramebuffer.hpp"
#include "integrators/Integrator.hpp"
#include "integrators/AOIntegrator.hpp"
#include "integrators/GeometryIntegrator.hpp"

namespace c2ba
{

class TileRenderer
{
public:
    ~TileRenderer()
    {
        stop();
    }

    void setScene(const RTScene & scene)
    {
        m_Integrator->setScene(scene);
        m_Dirty = true;
    }

    void setFramebuffer(size_t fbWidth, size_t fbHeight)
    {
        m_Framebuffer = TiledFramebuffer(s_TileSize, fbWidth, fbHeight);
        m_Image.resize(fbWidth * fbHeight);

        m_TilePermutation.resize(m_Framebuffer.tileCount());
        std::iota(begin(m_TilePermutation), end(m_TilePermutation), 0u);

        m_TileSampleCount.resize(m_Framebuffer.tileCount());
        std::fill(begin(m_TileSampleCount), end(m_TileSampleCount), 0);

        std::random_device rd;
        std::mt19937 g{ rd() };

        std::shuffle(begin(m_TilePermutation), end(m_TilePermutation), g);

        m_Integrator->setFramebufferSize(fbWidth, fbHeight);
        m_Dirty = true;
    }

    void setProjMatrix(const float4x4 & projMatrix)
    {
        m_Integrator->setProjMatrix(projMatrix);
        m_Dirty = true;
    }

    void setViewMatrix(const float4x4 & viewMatrix)
    {
        m_Integrator->setViewMatrix(viewMatrix);
        m_Dirty = true;
    }

    void clear()
    {
        fill(begin(m_Image), end(m_Image), float4(0));
        m_Framebuffer.clear();
        m_Dirty = false;
        m_NextTile = 0;
    }

    // Bake rendered tiled framebuffer to contiguously allocated image
    void bake()
    {
        if (m_bStopped || m_bPaused)
        {
            if (m_Dirty) {
                clear();
            }
            return;
        }

        if (m_Dirty) {
            pause();
            clear();
            start();
        }

        m_Framebuffer.copy(m_Image.data());
    }

    // Start the rendering if a scene has been set and the renderer is stopped or paused.
    bool start()
    {
        if (!m_bStopped && !m_bPaused)
            return false;

        if (m_bStopped)
        {
            m_bStopped = false;
            m_bPaused = false;
            m_ThreadCount = getHardwareConcurrency() > 1u ? getHardwareConcurrency() - 1u : 1u; // Try to keep one thread for the main loop
            m_RenderTaskFuture = asyncParallelRun(m_ThreadCount, [this](size_t threadId) { renderTask(threadId); });
        }
        else if (m_bPaused)
        {
            m_bPaused = false;
            m_PausedThreadCount = 0;
            m_UnpauseCondition.notify_all();
        }

        return true;
    }

    // All threads wait for the next call to start() to resume rendering.
    // The method waits for all threads to be in the waiting state
    void pause()
    {
        if (m_bPaused || m_bStopped)
            return;

        m_PausedThreadCount = 0;
        m_bPaused = true;
        while (m_PausedThreadCount != m_ThreadCount) // Wait for all threads to increment the counter
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    }

    // All threads exit their rendering function.
    // The method waits for them.
    void stop()
    {
        m_bStopped = true;
        m_bPaused = false;
        m_PausedThreadCount = 0;
        m_UnpauseCondition.notify_all();

        m_RenderTaskFuture.wait();
        m_ThreadCount = 0;
    }

    const float4 * getPixels() const
    {
        return m_Image.data();
    }

private:
    void renderTask(size_t threadId)
    {
        std::mt19937 g{ (unsigned int)threadId };
        std::uniform_real_distribution<float> d;

        while (!m_bStopped)
        {
            if (m_bPaused && !m_bStopped) {
                ++m_PausedThreadCount;
                std::unique_lock<std::mutex> l{ m_UnpauseMutex };
                m_UnpauseCondition.wait(l, [this]() { return !(m_bPaused && !m_bStopped); });
            }

            if (m_bStopped) {
                break;
            }
            const auto tileId = m_TilePermutation[m_NextTile++ % m_Framebuffer.tileCount()];
            const auto l = m_Framebuffer.lockTile(tileId);

            const auto bounds = m_Framebuffer.tileBounds(tileId);
            float4 * tilePtr = m_Framebuffer.tileDataPtr(tileId);

            m_Integrator->render(m_TileSampleCount[tileId], 1, bounds.beginX, bounds.beginY, bounds.countX, bounds.countY, tilePtr);
            ++m_TileSampleCount[tileId];

            if (m_bStopped) {
                break;
            }
        }
    }

    bool m_bPaused = false;
    bool m_bStopped = true;
    bool m_Dirty = true;

    static const size_t s_TileSize = 16;
    TiledFramebuffer m_Framebuffer;

    std::vector<size_t> m_TilePermutation;
    std::vector<size_t> m_TileSampleCount;

    std::vector<float4> m_Image;

    std::atomic_uint32_t m_NextTile{ 0 };
    std::future<void> m_RenderTaskFuture;

    uint32_t m_ThreadCount{ 0 };
    std::atomic_uint32_t m_PausedThreadCount{ 0 };

    std::mutex m_UnpauseMutex;
    std::condition_variable m_UnpauseCondition;

    std::unique_ptr<Integrator> m_Integrator = std::make_unique<AOIntegrator>();
};

}