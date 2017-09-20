#pragma once

#include <random>

#include "Integrator.hpp"

namespace c2ba
{

class GeometryIntegrator : public Integrator
{
    void doPreprocess() override;

    void doRender(const RenderTileParams & params) override;

    std::vector<std::mt19937> m_RandomGenerators;
};

}