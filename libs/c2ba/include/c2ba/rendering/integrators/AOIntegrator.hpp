#pragma once

#include <random>

#include "Integrator.hpp"

namespace c2ba
{

class AOIntegrator : public Integrator
{
public:
    void render(size_t startSample, size_t sampleCount, size_t startX, size_t startY, size_t countX, size_t countY, float4 * outBuffer) const override;

private:
    void renderAO(const float2 & rasterPos, float4 * pixelPtr, std::mt19937 & g, const std::uniform_real_distribution<float> & d) const;
};

}