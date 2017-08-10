#pragma once

#include <vector>
#include <tuple>

#include <embree2/rtcore_builder.h>
#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>

#include "../maths.hpp"

namespace c2ba
{

struct SceneGeometry
{
    struct Vertex
    {
        float3 position, normal;
        float2 texCoords;

        Vertex() = default;
        Vertex(float3 p, float3 n, float2 t) : position(p), normal(n), texCoords(t) {}
    };

    struct Triangle
    {
        uint32_t v0, v1, v2;
        Triangle() = default;
        Triangle(uint32_t v0, uint32_t v1, uint32_t v2) : v0(v0), v1(v1), v2(v2) {}
    };

    std::vector<Vertex> m_Vertices;
    std::vector<Triangle> m_Triangles;
    std::vector<std::tuple<size_t, size_t, size_t>> m_Meshes; // List of pairs (triangle offset, triangle count, vertex count)

    void append(const Vertex * pVertices, size_t vertexCount, const Triangle * pTriangles, size_t triangleCount)
    {
        const auto offset = m_Vertices.size();
        m_Vertices.insert(end(m_Vertices), pVertices, pVertices + vertexCount);

        const auto triangleOffset = m_Triangles.size();
        for (size_t triangleIdx = 0; triangleIdx < triangleCount; ++triangleIdx)
        {
            const auto & triangle = pTriangles[triangleIdx];
            m_Triangles.emplace_back(offset + triangle.v0, offset + triangle.v1, offset + triangle.v2);
        }

        m_Meshes.emplace_back(triangleOffset, triangleCount, vertexCount);
    }

    size_t getMaterialCount()
    {
        return 0;
    }
};


struct RTScene
{
    RTCScene m_rtcScene;

    RTScene(const RTCDevice & device, const SceneGeometry & geometry) : m_rtcScene{ rtcDeviceNewScene(device, RTC_SCENE_STATIC, RTC_INTERSECT1) }
    {
        for (size_t i = 0; i < geometry.m_Meshes.size(); ++i)
        {
            const auto geomId = rtcNewTriangleMesh2(m_rtcScene, RTC_GEOMETRY_STATIC, std::get<1>(geometry.m_Meshes[i]), geometry.m_Vertices.size());
            rtcSetBuffer2(m_rtcScene, geomId, RTC_VERTEX_BUFFER, geometry.m_Vertices.data(), 0, sizeof(SceneGeometry::Vertex), geometry.m_Vertices.size());
            rtcSetBuffer2(m_rtcScene, geomId, RTC_INDEX_BUFFER, geometry.m_Triangles.data(), sizeof(SceneGeometry::Triangle) * std::get<0>(geometry.m_Meshes[i]),
                sizeof(SceneGeometry::Triangle), std::get<1>(geometry.m_Meshes[i]));
        }
        rtcCommit(m_rtcScene);
    }

    ~RTScene()
    {
        rtcDeleteScene(m_rtcScene);
    }
};

SceneGeometry loadModel(const std::string& filepath);

}