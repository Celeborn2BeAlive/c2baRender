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

    void setProjMatrix(const float4x4 & projMatrix)
    {
        m_RcpProjMatrix = inverse(projMatrix);
    }

    void setViewMatrix(const float4x4 & viewMatrix)
    {
        m_RcpViewMatrix = inverse(viewMatrix);
    }

    void setFramebufferSize(size_t width, size_t height)
    {
        m_nFramebufferWidth = width;
        m_nFramebufferHeight = height;
    }

    virtual void preprocess() {}

    virtual void render(size_t startSample, size_t sampleCount, size_t startX, size_t startY, size_t countX, size_t countY, float4 * outBuffer) const = 0;

protected:
    const RTScene * m_Scene = nullptr;

    float4x4 m_RcpProjMatrix; // Screen to Cam
    float4x4 m_RcpViewMatrix; // Cam to World

    size_t m_nFramebufferWidth = 0;
    size_t m_nFramebufferHeight = 0;
};

}