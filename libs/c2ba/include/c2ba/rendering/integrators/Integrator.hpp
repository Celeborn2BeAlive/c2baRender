#pragma once

#include <c2ba/maths.hpp>
#include <c2ba/scene/Scene.hpp>

namespace c2ba
{

class Integrator
{
public:
    virtual ~Integrator() {}

    void setScene(const RTScene & scene)
    {
        m_Scene = &scene;
    }

    void setProjMatrix(const float4x4 & projMatrix) // to be replace by a Sensor
    {
        m_RcpProjMatrix = inverse(projMatrix);
    }

    void setViewMatrix(const float4x4 & viewMatrix) // to be kept, define Sensor position
    {
        m_RcpViewMatrix = inverse(viewMatrix);
    }

    void setFramebufferSize(size_t width, size_t height)
    {
        m_nFramebufferWidth = width;
        m_nFramebufferHeight = height;
    }

    void setTileSize(size_t size)
    {
        m_nTileSize = size;
    }

    struct RenderTileParams
    {
        size_t tileId;
        size_t startSample;
        size_t sampleCount;
        size_t beginX, beginY; // lower left pixel
        size_t countX, countY; // number of pixels

        float4 * outBuffer;
    };

    // After all setters have been called, must be called to preprocess data required for rendering
    void preprocess()
    {
        m_nTileCountX = m_nFramebufferWidth / m_nTileSize + size_t{ (m_nFramebufferWidth % m_nTileSize) != 0 };
        m_nTileCountY = m_nFramebufferHeight / m_nTileSize + size_t{ (m_nFramebufferHeight % m_nTileSize) != 0 };
        m_nTileCount = m_nTileCountX * m_nTileCountY;

        doPreprocess();
    }

    // Render pixels of a tile. This method should not be called by multiple threads at the same time for a given tile.
    void render(const RenderTileParams & params) const
    {
        doRender(params);
    }

private:
    virtual void doPreprocess() {}

    virtual void doRender(const RenderTileParams & params) const = 0;

protected:
    const RTScene * m_Scene = nullptr;

    float4x4 m_RcpProjMatrix; // Screen to Cam
    float4x4 m_RcpViewMatrix; // Cam to World

    size_t m_nFramebufferWidth = 0;
    size_t m_nFramebufferHeight = 0;

    size_t m_nTileSize = 0;

    size_t m_nTileCountX = 0;
    size_t m_nTileCountY = 0;
    size_t m_nTileCount;
};

}