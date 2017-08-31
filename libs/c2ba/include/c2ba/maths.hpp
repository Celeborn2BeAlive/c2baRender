#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/io.hpp>

namespace c2ba
{

// types

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using float4x4 = glm::mat4;

// numeric functions

using glm::abs;
using glm::sqrt;
using glm::cos;
using glm::sin;
using glm::max;
using glm::min;

// constants

using glm::pi;

// additional functions

inline float3 getColor(size_t n) {
    return fract(
        sin(
            float(n + 1) * float3(12.9898, 78.233, 56.128)
            )
        * 43758.5453f
        );
}

inline float3 sampleHemisphereCosine(float u1, float u2)
{
    const float r = sqrt(u1);
    const float theta = 2.f * pi<float>() * u2;

    const float x = r * cos(theta);
    const float y = r * sin(theta);

    return float3(x, y, sqrt(max(0.0f, 1 - u1)));
}

inline void makeOrthonormals(const float3 &n, float3 &b1, float3 &b2)
{
    float sign = std::copysignf(1.0f, n.z);
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    b1 = float3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = float3(b, sign + n.y * n.y * a, -n.y);
}
inline float3 faceForward(const float3 & v, const float3 & ref)
{
    if (dot(v, ref) < 0.f)
        return -v;
    return v;
}

}