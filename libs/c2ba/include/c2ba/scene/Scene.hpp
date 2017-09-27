#pragma once

#include <vector>
#include <tuple>
#include <iostream>

#include <embree2/rtcore_builder.h>
#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>

#include "../maths.hpp"

namespace c2ba
{

struct alignas(RTCRay) Ray
{
    static constexpr uint32_t InvalidID = RTC_INVALID_GEOMETRY_ID;

    float3 org;
    float _alignOrg;

    float3 dir;
    float _alignDir;

    float tnear;
    float tfar;

    float time;
    uint32_t mask;

    float3 Ng;
    float _alignNg;

    float u;
    float v;

    uint32_t geomID = InvalidID;
    uint32_t primID = InvalidID;
    uint32_t instID = InvalidID;

    Ray() = default;

    Ray(const float3 & org, const float3 & dir,
        float tnear = 0.f, float tfar = std::numeric_limits<float>::infinity(),
        float time = 0.f, uint32_t mask = 0xFFFFFFFF);
};

inline float3 hitPoint(const Ray & ray)
{
    return ray.org + ray.tfar * ray.dir;
}

template<size_t N>
using RaySOA = RTCRayNt<N>;

using RaySOAPtrs = RTCRayNp;

template<size_t N>
RaySOAPtrs raySOAPtrs(RaySOA<N> & rays)
{
    RaySOAPtrs ptrs;

    ptrs.orgx = rays.orgx;
    ptrs.orgy = rays.orgy;
    ptrs.orgz = rays.orgz;
    ptrs.dirx = rays.dirx;
    ptrs.diry = rays.diry;
    ptrs.dirz = rays.dirz;
    ptrs.tnear = rays.tnear;
    ptrs.tfar = rays.tfar;
    ptrs.time = rays.time;
    ptrs.mask = rays.mask;
    ptrs.Ngx = rays.Ngx;
    ptrs.Ngy = rays.Ngy;
    ptrs.Ngz = rays.Ngz;
    ptrs.geomID = rays.geomID;
    ptrs.instID = rays.instID;
    ptrs.primID = rays.primID;

    return ptrs;
}

inline void advance(RaySOAPtrs & ptrs, size_t stride)
{
    const size_t byteSize = sizeof(RaySOAPtrs);
    const size_t ptrCount = byteSize / sizeof(void*);
    for (size_t i = 0; i < ptrCount; ++i)
        ((char**)(&ptrs))[i] += stride;
}

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

const RTCDevice & getRTCDevice();

struct RTScene
{
    RTCScene m_rtcScene;

    RTScene(const SceneGeometry & geometry) : 
        m_rtcScene{ rtcDeviceNewScene(getRTCDevice(), RTC_SCENE_STATIC, RTC_INTERSECT1 | RTC_INTERSECT4 | RTC_INTERSECT8 | RTC_INTERSECT16 | RTC_INTERSECT_STREAM) }
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

struct HitPointParams
{
    const Ray & ray;
    const SceneGeometry::Vertex & v0;
    const SceneGeometry::Vertex & v1;
    const SceneGeometry::Vertex & v2;
    float w;

    HitPointParams(
        const Ray & ray,
        const SceneGeometry::Vertex & v0,
        const SceneGeometry::Vertex & v1,
        const SceneGeometry::Vertex & v2,
        float w):
        ray(ray), v0(v0), v1(v1), v2(v2), w(w)
    {}
};

template<typename T>
struct HitPointAttribute
{
    using Type = T;

    T & ref;
    HitPointAttribute(T & ref) : ref(ref) {}
};

struct Position: public HitPointAttribute<float3>
{
    using HitPointAttribute::HitPointAttribute;

    void set(const HitPointParams & params)
    {
        ref = params.v0.position * params.w + params.v1.position * params.ray.u + params.v2.position * params.ray.v;
    }
};

struct Normal : public HitPointAttribute<float3>
{
    using HitPointAttribute::HitPointAttribute;

    void set(const HitPointParams & params)
    {
        ref = faceForward(normalize(params.v0.normal * params.w + params.v1.normal * params.ray.u + params.v2.normal * params.ray.v), -params.ray.dir);
    }
};

struct TriangleNormal : public HitPointAttribute<float3>
{
    using HitPointAttribute::HitPointAttribute;

    void set(const HitPointParams & params)
    {
        ref = faceForward(normalize(params.ray.Ng), -params.ray.dir);
    }
};

struct TexCoords : public HitPointAttribute<float2>
{
    using HitPointAttribute::HitPointAttribute;

    void set(const HitPointParams & params)
    {
        ref = normalize(params.v0.texCoords * params.w + params.v1.texCoords * params.ray.u + params.v2.texCoords * params.ray.v);
    }
};

enum class Facing
{
    Front,
    Back,
    Undefined
};

struct TriangleFacing : public HitPointAttribute<Facing>
{
    using HitPointAttribute::HitPointAttribute;

    void set(const HitPointParams & params)
    {
        const auto Ng = cross(params.v1.position - params.v0.position, params.v2.position - params.v0.position);
        ref = dot(params.ray.dir, Ng) > 0.f ? Facing::Back : Facing::Front;
    }
};

enum class RayProperties
{
    Coherent = RTC_INTERSECT_COHERENT,
    Incoherent = RTC_INTERSECT_INCOHERENT
};

class Scene
{
public:
    Scene(SceneGeometry geometry) :
        m_Geometry(std::move(geometry))
    {
    }

    bool intersect(Ray & ray) const
    {
        rtcIntersect(m_Accel.m_rtcScene, (RTCRay&)ray);
        return ray.geomID != Ray::InvalidID;
    }

    bool occluded(Ray & ray) const
    {
        rtcOccluded(m_Accel.m_rtcScene, (RTCRay&)ray);
        return ray.geomID == 0; // If some geometry got found along the ray segment, the geometry ID (geomID) will get set to 0 http://embree.github.io/api.html
    }

    void intersect(Ray * rays, size_t count, RayProperties properties) const
    {
        RTCIntersectContext context;
        context.flags = RTCIntersectFlags(properties);
        context.userRayExt = nullptr;
        rtcIntersectNM(m_Accel.m_rtcScene, &context, (RTCRayN*)rays, 1, count, sizeof(Ray));
    }

    void occluded(Ray * rays, size_t count, RayProperties properties) const
    {
        RTCIntersectContext context;
        context.flags = RTCIntersectFlags(properties);
        context.userRayExt = nullptr;
        rtcOccludedNM(m_Accel.m_rtcScene, &context, (RTCRayN*)rays, 1, count, sizeof(Ray));
    }

    template<size_t RayCount>
    void intersect(RaySOA<RayCount> * rayPackets, size_t packetCount, RayProperties properties) const
    {
        RTCIntersectContext context;
        context.flags = RTCIntersectFlags(properties);
        context.userRayExt = nullptr;
        rtcIntersectNM(m_Accel.m_rtcScene, &context, (RTCRayN*)rayPackets, RayCount, packetCount, sizeof(RaySOA<RayCount>));
    }

    template<size_t RayCount>
    void occluded(RaySOA<RayCount> * rayPackets, size_t packetCount, RayProperties properties) const
    {
        RTCIntersectContext context;
        context.flags = RTCIntersectFlags(properties);
        context.userRayExt = nullptr;
        rtcOccludedNM(m_Accel.m_rtcScene, &context, (RTCRayN*)rayPackets, RayCount, packetCount, sizeof(RaySOA<RayCount>));
    }

    void intersect(RaySOAPtrs & rays, size_t count, RayProperties properties) const
    {
        RTCIntersectContext context;
        context.flags = RTCIntersectFlags(properties);
        context.userRayExt = nullptr;
        rtcIntersectNp(m_Accel.m_rtcScene, &context, rays, count);
    }

    void occluded(RaySOAPtrs & rays, size_t count, RayProperties properties) const
    {
        RTCIntersectContext context;
        context.flags = RTCIntersectFlags(properties);
        context.userRayExt = nullptr;
        rtcOccludedNp(m_Accel.m_rtcScene, &context, rays, count);
    }

    const SceneGeometry & geometry() const
    {
        return m_Geometry;
    }

    template<typename... HitPointAttributes>
    void evalHitPoint(const Ray & ray, HitPointAttributes && ... attrs) const
    {
        const auto & mesh = m_Geometry.m_Meshes[ray.geomID];
        const auto triangleOffset = std::get<0>(mesh);
        const auto & triangle = m_Geometry.m_Triangles[triangleOffset + ray.primID];

        const HitPointParams params{
            ray,
            m_Geometry.m_Vertices[triangle.v0],
            m_Geometry.m_Vertices[triangle.v1],
            m_Geometry.m_Vertices[triangle.v2],
            1.f - ray.u - ray.v
        };

        interpolate(params, std::forward<HitPointAttributes>(attrs)...);
    }

private:
    void interpolate(const HitPointParams & params) const
    {
        // End of compile time recursion
    }

    template<typename HitPointAttribute, typename... HitPointAttributes>
    void interpolate(const HitPointParams & params, HitPointAttribute && attr, HitPointAttributes && ... attrs) const
    {
        attr.set(params);
        interpolate(params, std::forward<HitPointAttributes>(attrs)...);
    }

    SceneGeometry m_Geometry;
    RTScene m_Accel{ m_Geometry };
};
}