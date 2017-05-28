#include <iostream>
#include <filesystem>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <tuple>
#include <algorithm>
#include <numeric>
#include <random>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <embree2/rtcore_builder.h>
#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>

#include "imgui_impl_glfw_gl3.hpp"
#include "GLProgram.hpp"
#include "ViewController.hpp"

namespace fs = std::experimental::filesystem;

using Vec3f = glm::vec3;
using Vec2f = glm::vec2;

struct TriangleMesh
{
    struct Vertex
    {
        Vec3f position, normal;
        Vec2f texCoords;

        Vertex() = default;
        Vertex(Vec3f p, Vec3f n, Vec2f t): position(p), normal(n), texCoords(t) {}
    };

    struct Triangle
    {
        uint32_t v0, v1, v2;
        Triangle() = default;
        Triangle(uint32_t v0, uint32_t v1, uint32_t v2) : v0(v0), v1(v1), v2(v2) {}
    };

    size_t m_MaterialID;
    std::vector<Vertex> m_Vertices;
    std::vector<Triangle> m_Triangles;
};

struct SceneGeometry
{
    std::vector<TriangleMesh::Vertex> m_Vertices;
    std::vector<TriangleMesh::Triangle> m_Triangles;
    std::vector<std::tuple<size_t, size_t, size_t>> m_Meshes; // List of pairs (offset, triangle count, vertex count)

    void append(const TriangleMesh & mesh)
    {
        const auto offset = m_Vertices.size();
        m_Vertices.insert(end(m_Vertices), begin(mesh.m_Vertices), end(mesh.m_Vertices));

        const auto triangleOffset = m_Triangles.size();

        for (const auto & triangle : mesh.m_Triangles)
        {
            m_Triangles.emplace_back(offset + triangle.v0, offset + triangle.v1, offset + triangle.v2);
        }

        m_Meshes.emplace_back(triangleOffset, mesh.m_Triangles.size(), mesh.m_Vertices.size());
    }

    size_t getMaterialCount()
    {
        return 0;
    }
};

struct Material
{
    Vec3f m_DiffuseReflectance;

    Material(std::string name)
    {
    }
};

SceneGeometry loadModel(const std::string& filepath);

struct RTScene
{
    RTCScene m_rtcScene;

    RTScene(const RTCDevice & device, const SceneGeometry & geometry): m_rtcScene { rtcDeviceNewScene(device, RTC_SCENE_STATIC, RTC_INTERSECT1) }
    {
        for (size_t i = 0; i < geometry.m_Meshes.size(); ++i)
        {
            const auto geomId = rtcNewTriangleMesh2(m_rtcScene, RTC_GEOMETRY_STATIC, std::get<1>(geometry.m_Meshes[i]), geometry.m_Vertices.size());
            rtcSetBuffer2(m_rtcScene, geomId, RTC_VERTEX_BUFFER, geometry.m_Vertices.data(), 0, sizeof(TriangleMesh::Vertex), geometry.m_Vertices.size());
            rtcSetBuffer2(m_rtcScene, geomId, RTC_INDEX_BUFFER, geometry.m_Triangles.data(), sizeof(TriangleMesh::Triangle) * std::get<0>(geometry.m_Meshes[i]), 
                sizeof(TriangleMesh::Triangle), std::get<1>(geometry.m_Meshes[i]));
        }
        rtcCommit(m_rtcScene);
    }

    ~RTScene()
    {
        rtcDeleteScene(m_rtcScene);
    }
};

glm::vec3 getColor(size_t n) {
    return glm::fract(
        glm::sin(
            float(n + 1) * glm::vec3(12.9898, 78.233, 56.128)
            )
        * 43758.5453f
        );
}

class TiledFramebuffer
{
public:
    struct TileBounds
    {
        size_t beginX;
        size_t beginY;
        size_t countX;
        size_t countY;
    };

    TiledFramebuffer() = default;

    TiledFramebuffer(size_t tileSize, size_t imageWidth, size_t imageHeight) :
        m_nTileSize{ tileSize }, m_nTilePixelCount{ m_nTileSize * m_nTileSize },
        m_nImageWidth{ imageWidth }, m_nImageHeight{ imageHeight }, m_nPixelCount{ m_nImageWidth * m_nImageHeight },
        m_nTileCountX{ (m_nImageWidth / m_nTileSize) + ((m_nImageWidth % m_nTileSize) ? 1 : 0) }, m_nTileCountY{ (m_nImageHeight / m_nTileSize) + ((m_nImageHeight % m_nTileSize) ? 1 : 0) },
        m_nTileCount{ m_nTileCountX * m_nTileCountY },
        m_Data(m_nTileCount * m_nTilePixelCount, glm::vec4(0.f) ),
        m_TileLocks{ m_nTileCount }
    {
    }

    std::unique_lock<std::mutex> lockTile(size_t tileIdx) const
    {
        return std::unique_lock<std::mutex>{ m_TileLocks[tileIdx] };
    }

    glm::vec4* tileDataPtr(size_t tileIdx)
    {
        return m_Data.data() + tileIdx * m_nTilePixelCount;
    }

    const glm::vec4* tileDataPtr(size_t tileIdx) const
    {
        return m_Data.data() + tileIdx * m_nTilePixelCount;
    }

    TileBounds tileBounds(size_t tileX, size_t tileY) const
    {
        const size_t beginX = tileX * m_nTileSize;
        const size_t endX = std::min((tileX + 1) * m_nTileSize, m_nImageWidth);

        const size_t beginY = tileY * m_nTileSize;
        const size_t endY = std::min((tileY + 1) * m_nTileSize, m_nImageHeight);

        const size_t countX = endX - beginX;
        const size_t countY = endY - beginY;

        assert(countX <= m_nTileSize);
        assert(countY <= m_nTileSize);

        return { beginX, beginY, countX, countY };
    }

    TileBounds tileBounds(size_t tileIdx) const
    {
        const auto tileX = tileIdx % m_nTileCountX;
        const auto tileY = tileIdx / m_nTileCountX;

        return tileBounds(tileX, tileY);
    }

    void copy(glm::vec4 * outImage) const
    {
        for (size_t tileIdx = 0u; tileIdx < m_nTileCount; ++tileIdx)
        {
            const auto bounds = tileBounds(tileIdx);
            const auto tileData = tileDataPtr(tileIdx);

            for (size_t tileY = 0; tileY < bounds.countY; ++tileY) {
                std::copy(tileData + tileY * m_nTileSize, tileData + tileY * m_nTileSize + bounds.countX, outImage + (bounds.beginY + tileY) * m_nImageWidth + bounds.beginX);
            }
        }
    }

    void clear()
    {
        for (size_t tileIdx = 0u; tileIdx < m_nTileCount; ++tileIdx)
        {
            const auto l = lockTile(tileIdx);
            const auto bounds = tileBounds(tileIdx);
            const auto tileData = tileDataPtr(tileIdx);
            std::fill(tileData, tileData + m_nTilePixelCount, glm::vec4(0.f));
        }
    }

    size_t tileSize() const
    {
        return m_nTileSize;
    }

    size_t tilePixelCount() const
    {
        return m_nTilePixelCount;
    }

    size_t imageWidth() const
    {
        return m_nImageWidth;
    }

    size_t imageHeight() const
    {
        return m_nImageHeight;
    }

    size_t pixelCount() const
    {
        return m_nPixelCount;
    }

    size_t tileCountX() const
    {
        return m_nTileCountX;
    }

    size_t tileCountY() const
    {
        return m_nTileCountY;
    }

    size_t tileCount() const
    {
        return m_nTileCount;
    }

private:
    size_t m_nTileSize = 0;
    size_t m_nTilePixelCount = 0;
    size_t m_nImageWidth = 0;
    size_t m_nImageHeight = 0;
    size_t m_nPixelCount = 0;

    size_t m_nTileCountX = 0;
    size_t m_nTileCountY = 0;
    size_t m_nTileCount = 0;

    std::vector<glm::vec4> m_Data;
    mutable std::vector<std::mutex> m_TileLocks;
};

glm::vec3 sampleHemisphereCosine(float u1, float u2)
{
    const float r = glm::sqrt(u1);
    const float theta = 2.f * glm::pi<float>() * u2;

    const float x = r * glm::cos(theta);
    const float y = r * glm::sin(theta);

    return glm::vec3(x, y, glm::sqrt(glm::max(0.0f, 1 - u1)));
}

void makeOrthonormals(const Vec3f &n, Vec3f &b1, Vec3f &b2)
{
    float sign = std::copysignf(1.0f, n.z);
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    b1 = Vec3f(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = Vec3f(b, sign + n.y * n.y * a, -n.y);
}
Vec3f faceForward(const Vec3f & v, const Vec3f & ref)
{
    if (dot(v, ref) < 0.f)
        return -v;
    return v;
}

class TileRenderer
{
public:
    TileRenderer()
    {
        const auto threadCount = std::thread::hardware_concurrency();
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
        for (auto & thread: m_ThreadPool) {
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

    void setProjMatrix(const glm::mat4 & projMatrix)
    {
        m_RcpProjMatrix = inverse(projMatrix);
        m_Dirty = true;
    }

    void setViewMatrix(const glm::mat4 & viewMatrix)
    {
        m_RcpViewMatrix = inverse(viewMatrix);
        m_Dirty = true;
    }

    void clear()
    {
        fill(begin(m_Image), end(m_Image), glm::vec4(0));
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

    const glm::vec4 * getPixels() const
    {
        return m_Image.data();
    }

private:
    bool m_bRunning = false;
    bool m_bDone = false;

    static const size_t s_TileSize = 16;
    TiledFramebuffer m_Framebuffer;

    void renderRasterPos(const glm::vec2 & rasterPos, glm::vec4 * pixelPtr)
    {
        *pixelPtr += glm::vec4(glm::vec2(rasterPos / glm::vec2(m_Framebuffer.imageWidth(), m_Framebuffer.imageHeight())), 0.f, 1.f);
    }

    void renderNg(const glm::vec2 & rasterPos, glm::vec4 * pixelPtr)
    {
        const glm::vec2 ndcPos = glm::vec2(-1) + 2.f * glm::vec2(rasterPos / glm::vec2(m_Framebuffer.imageWidth(), m_Framebuffer.imageHeight()));

        glm::vec4 viewSpacePos = m_RcpProjMatrix * glm::vec4(ndcPos, -1.f, 1.f);
        viewSpacePos /= viewSpacePos.w;

        glm::vec4 worldSpacePos = m_RcpViewMatrix * viewSpacePos;
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
            *pixelPtr += glm::vec4(glm::abs(glm::vec3(ray.Ng[0], ray.Ng[1], ray.Ng[2])), 1);
        else
            *pixelPtr += glm::vec4(glm::vec3(0), 1);
    }

    void renderAO(const glm::vec2 & rasterPos, glm::vec4 * pixelPtr, std::mt19937 & g, const std::uniform_real_distribution<float> & d)
    {
        const glm::vec2 ndcPos = glm::vec2(-1) + 2.f * glm::vec2(rasterPos / glm::vec2(m_Framebuffer.imageWidth(), m_Framebuffer.imageHeight()));

        glm::vec4 viewSpacePos = m_RcpProjMatrix * glm::vec4(ndcPos, -1.f, 1.f);
        viewSpacePos /= viewSpacePos.w;

        glm::vec4 worldSpacePos = m_RcpViewMatrix * viewSpacePos;
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
            const auto Ng = faceForward(normalize(glm::vec3(ray.Ng[0], ray.Ng[1], ray.Ng[2])), -glm::vec3(ray.dir[0], ray.dir[1], ray.dir[2]));
            Vec3f Tx, Ty;
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
                    const Vec3f localDir = sampleHemisphereCosine(u1, u2);
                    const Vec3f worldDir = localDir.x * Tx + localDir.y * Ty + localDir.z * Ng;

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

            *pixelPtr += glm::vec4(glm::vec3(visibility), 1);

        }
        else
            *pixelPtr += glm::vec4(glm::vec3(0), 1);
    }

    void renderTask(size_t threadId)
    {
        std::mt19937 g{ (unsigned int) threadId };
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
            glm::vec4 * tilePtr = m_Framebuffer.tileDataPtr(tileId);

            for (size_t pixelY = 0; pixelY < bounds.countY; ++pixelY)
            {
                for (size_t pixelX = 0; pixelX < bounds.countX; ++pixelX)
                {
                    const size_t pixelId = pixelX + pixelY * s_TileSize;

                    const glm::vec2 rasterPos = glm::vec2(bounds.beginX + pixelX + 0.5f, bounds.beginY + pixelY + 0.5f);
                    const glm::vec2 ndcPos = glm::vec2(-1) + 2.f * glm::vec2(rasterPos / glm::vec2(m_Framebuffer.imageWidth(), m_Framebuffer.imageHeight()));

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
    std::vector<glm::vec4> m_Image;

    const RTScene * m_Scene = nullptr;

    glm::mat4 m_RcpProjMatrix; // Screen to Cam
    glm::mat4 m_RcpViewMatrix; // Cam to World

    std::atomic<uint32_t> m_NextTile{ 0 };
};

void rtcErrorCallback(void* userPtr, const RTCError code, const char * str)
{
    std::cerr << "Embree error: " << str << std::endl;
}

template<typename DeleteFunc>
struct RAII
{
    DeleteFunc m_DelF;
    RAII(DeleteFunc delF) : m_DelF(std::move(delF))
    {
    }

    ~RAII()
    {
        m_DelF();
    }

    RAII(const RAII &) = delete;
    RAII & operator =(const RAII &) = delete;
    RAII(RAII &&) = default;
    RAII & operator =(RAII &&) = default;
};

template<typename DeleteFunc>
RAII<DeleteFunc> finally(DeleteFunc && delF)
{
    return RAII<DeleteFunc>(delF);
}

int main(int argc, char** argv)
{
    const auto m_AppPath = fs::path{ argv[0] };
    const auto m_AppName = fs::path{ m_AppPath.stem().string() };
    const auto appDir = m_AppPath.parent_path();
    const auto m_ShadersRootPath{ appDir / "shaders" };

    if (argc < 2)
    {
        std::cerr << "Usage : " << m_AppPath << " < path_to_scene >" << std::endl;
        return -1;
    }

    SceneGeometry geometry = loadModel(argv[1]);

    const RTCDevice embreeDevice = rtcNewDevice();
    const auto embreeDeviceDel = finally([embreeDevice]() { rtcDeleteDevice(embreeDevice); });
    rtcDeviceSetErrorFunction2(embreeDevice, rtcErrorCallback, nullptr);

    RTScene scene(embreeDevice, geometry);

    if (!glfwInit()) {
        std::cerr << "Unable to init GLFW.\n";
        throw std::runtime_error("Unable to init GLFW.\n");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    const size_t m_nWindowWidth = 1280;
    const size_t m_nWindowHeight = 720;

    const auto m_pWindow = glfwCreateWindow(int(m_nWindowWidth), int(m_nWindowHeight), "c2baRender", NULL, NULL);
    if (!m_pWindow) {
        std::cerr << "Unable to open window.\n";
        glfwTerminate();
        throw std::runtime_error("Unable to open window.\n");
    }

    glfwMakeContextCurrent(m_pWindow);

    glfwSwapInterval(0);

    if (!gladLoadGL()) {
        std::cerr << "Unable to init OpenGL.\n";
        throw std::runtime_error("Unable to init OpenGL.\n");
    }

    ImGui_ImplGlfwGL3_Init(m_pWindow, true);

    GLuint m_SceneVBO, m_SceneIBO, m_SceneVAO;
    glGenBuffers(1, &m_SceneVBO);
    glGenBuffers(1, &m_SceneIBO);

    glBindBuffer(GL_ARRAY_BUFFER, m_SceneVBO);
    glBufferStorage(GL_ARRAY_BUFFER, geometry.m_Vertices.size() * sizeof(TriangleMesh::Vertex), geometry.m_Vertices.data(), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ARRAY_BUFFER, m_SceneIBO);
    glBufferStorage(GL_ARRAY_BUFFER, geometry.m_Triangles.size() * sizeof(TriangleMesh::Triangle), geometry.m_Triangles.data(), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenVertexArrays(1, &m_SceneVAO);
    glBindVertexArray(m_SceneVAO);

    const GLint positionAttrLocation = 0;
    const GLint normalAttrLocation = 1;
    const GLint texCoordsAttrLocation = 2;

    // We tell OpenGL what vertex attributes our VAO is describing:
    glEnableVertexAttribArray(positionAttrLocation);
    glEnableVertexAttribArray(normalAttrLocation);
    glEnableVertexAttribArray(texCoordsAttrLocation);

    glBindBuffer(GL_ARRAY_BUFFER, m_SceneVBO); // We bind the VBO because the next 3 calls will read what VBO is bound in order to know where the data is stored

    glVertexAttribPointer(positionAttrLocation, 3, GL_FLOAT, GL_FALSE, sizeof(TriangleMesh::Vertex), (const GLvoid*)offsetof(TriangleMesh::Vertex, position));
    glVertexAttribPointer(normalAttrLocation, 3, GL_FLOAT, GL_FALSE, sizeof(TriangleMesh::Vertex), (const GLvoid*)offsetof(TriangleMesh::Vertex, normal));
    glVertexAttribPointer(texCoordsAttrLocation, 2, GL_FLOAT, GL_FALSE, sizeof(TriangleMesh::Vertex), (const GLvoid*)offsetof(TriangleMesh::Vertex, texCoords));

    glBindBuffer(GL_ARRAY_BUFFER, 0); // We can unbind the VBO because OpenGL has "written" in the VAO what VBO it needs to read when the VAO will be drawn

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_SceneIBO); // Binding the IBO to GL_ELEMENT_ARRAY_BUFFER while a VAO is bound "writes" it in the VAO for usage when the VAO will be drawn

    glBindVertexArray(0);

    GLuint m_TriangleVBO, m_TriangleVAO;
    glGenBuffers(1, &m_TriangleVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_TriangleVBO);

    GLfloat data[] = { -1, -1, 3, -1, -1, 3 };
    glBufferStorage(GL_ARRAY_BUFFER, sizeof(data), data, 0);

    glGenVertexArrays(1, &m_TriangleVAO);
    glBindVertexArray(m_TriangleVAO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    GLuint m_FramebufferTexture;
    glGenTextures(1, &m_FramebufferTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_FramebufferTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, m_nWindowWidth, m_nWindowHeight);

    const auto m_program = compileProgram({ m_ShadersRootPath / m_AppName / "forward.vs.glsl", m_ShadersRootPath / m_AppName / "forward.fs.glsl" });
    
    const auto m_uModelViewProjMatrixLocation = glGetUniformLocation(m_program.glId(), "uModelViewProjMatrix");
    const auto m_uModelViewMatrixLocation = glGetUniformLocation(m_program.glId(), "uModelViewMatrix");
    const auto m_uNormalMatrixLocation = glGetUniformLocation(m_program.glId(), "uNormalMatrix");

    const auto m_drawQuadProgram = compileProgram({ m_ShadersRootPath / m_AppName / "draw_quad.vs.glsl", m_ShadersRootPath / m_AppName / "draw_quad.fs.glsl" });

    const auto m_uImage = glGetUniformLocation(m_drawQuadProgram.glId(), "uImage");
    glProgramUniform1i(m_drawQuadProgram.glId(), m_uImage, 0);

    ViewController m_viewController{ m_pWindow };
    m_viewController.setViewMatrix(glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0)));
    m_viewController.setSpeed(3000.f * 0.1f);

    TileRenderer renderer;
    renderer.setFramebuffer(m_nWindowWidth, m_nWindowHeight);

    const auto projMatrix = glm::perspective(glm::radians(70.f), float(m_nWindowWidth) / m_nWindowHeight, 0.01f * 3000.f, 3000.f);
    renderer.setProjMatrix(projMatrix);

    renderer.setScene(scene);

    bool cameraMoved = true;

    for (auto iterationCount = 0u; !glfwWindowShouldClose(m_pWindow); ++iterationCount)
    {
        auto seconds = glfwGetTime();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const auto viewMatrix = m_viewController.getViewMatrix();

        const auto modelMatrix = glm::mat4();

        const auto mvMatrix = viewMatrix * modelMatrix;
        const auto mvpMatrix = projMatrix * mvMatrix;
        const auto normalMatrix = glm::transpose(glm::inverse(mvMatrix));

        glEnable(GL_DEPTH_TEST);
        m_program.use();

        glUniformMatrix4fv(m_uModelViewProjMatrixLocation, 1, GL_FALSE, glm::value_ptr(mvpMatrix));
        glUniformMatrix4fv(m_uModelViewMatrixLocation, 1, GL_FALSE, glm::value_ptr(mvMatrix));
        glUniformMatrix4fv(m_uNormalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));

        glBindVertexArray(m_SceneVAO);

        glDrawElements(GL_TRIANGLES, geometry.m_Triangles.size() * 3, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);

        if (!cameraMoved)
        {
            renderer.render();

            glDisable(GL_DEPTH_TEST);
            m_drawQuadProgram.use();

            glBindTexture(GL_TEXTURE_2D, m_FramebufferTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_nWindowWidth, m_nWindowHeight, GL_RGBA, GL_FLOAT, renderer.getPixels());

            glBindVertexArray(m_TriangleVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
        }
        else
        {
            renderer.setViewMatrix(m_viewController.getViewMatrix());
        }

        ImGui_ImplGlfwGL3_NewFrame();

        {
            ImGui::Begin("Params");
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // Rendering
        int display_w, display_h;
        glfwGetFramebufferSize(m_pWindow, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        ImGui::Render();

        /* Poll for and process events */
        glfwPollEvents();

        /* Swap front and back buffers*/
        glfwSwapBuffers(m_pWindow);

        auto ellapsedTime = glfwGetTime() - seconds;
        auto guiHasFocus = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
        cameraMoved = false;
        if (!guiHasFocus && m_viewController.update(float(ellapsedTime))) {
            cameraMoved = true;
        }
    }

    renderer.stop();

    ImGui_ImplGlfwGL3_Shutdown();
    glfwTerminate();

    return 0;
}

static const aiVector3D aiZERO(0.f, 0.f, 0.f);

static void loadMaterial(const aiMaterial* aimaterial, const fs::path& basePath, SceneGeometry& geometry)
{
    //aiColor3D color;

    //aiString ainame;
    //aimaterial->Get(AI_MATKEY_NAME, ainame);
    //std::string name = ainame.C_Str();

    //std::clog << "Load material " << name << std::endl;

    //Material material(name);

    //if (AI_SUCCESS == aimaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color)) {
    //    material.m_DiffuseReflectance = Vec3f(color.r, color.g, color.b);
    //}

    //aiString path;

    //if (AI_SUCCESS == aimaterial->GetTexture(aiTextureType_DIFFUSE, 0, &path,
    //    nullptr, nullptr, nullptr, nullptr, nullptr)) {
    //    pLogger->verbose(1, "Load texture %v", (basePath + path.data));
    //    material.m_DiffuseReflectanceTexture = loadImage(basePath + path.data, true);
    //}

    //if (AI_SUCCESS == aimaterial->Get(AI_MATKEY_COLOR_SPECULAR, color)) {
    //    material.m_GlossyReflectance = Vec3f(color.r, color.g, color.b);
    //}

    //if (AI_SUCCESS == aimaterial->GetTexture(aiTextureType_SPECULAR, 0, &path,
    //    nullptr, nullptr, nullptr, nullptr, nullptr)) {
    //    pLogger->verbose(1, "Load texture %v", (basePath + path.data));
    //    material.m_GlossyReflectanceTexture = loadImage(basePath + path.data, true);
    //}

    //aimaterial->Get(AI_MATKEY_SHININESS, material.m_Shininess);

    //if (AI_SUCCESS == aimaterial->GetTexture(aiTextureType_SHININESS, 0, &path,
    //    nullptr, nullptr, nullptr, nullptr, nullptr)) {
    //    pLogger->verbose(1, "Load texture %v", (basePath + path.data));
    //    material.m_ShininessTexture = loadImage(basePath + path.data, true);
    //}

    //geometry.addMaterial(std::move(material));
}

static void loadMesh(const aiMesh* aimesh, uint32_t materialOffset, SceneGeometry& geometry) {
    TriangleMesh mesh;

#ifdef _DEBUG
    mesh.m_MaterialID = 0;
#else
    mesh.m_MaterialID = materialOffset + aimesh->mMaterialIndex;
#endif

    mesh.m_Vertices.reserve(aimesh->mNumVertices);
    for (size_t vertexIdx = 0; vertexIdx < aimesh->mNumVertices; ++vertexIdx) {
        const aiVector3D* pPosition = aimesh->HasPositions() ? &aimesh->mVertices[vertexIdx] : &aiZERO;
        const aiVector3D* pNormal = aimesh->HasNormals() ? &aimesh->mNormals[vertexIdx] : &aiZERO;
        const aiVector3D* pTexCoords = aimesh->HasTextureCoords(0) ? &aimesh->mTextureCoords[0][vertexIdx] : &aiZERO;

        mesh.m_Vertices.emplace_back(
            Vec3f(pPosition->x, pPosition->y, pPosition->z),
            Vec3f(pNormal->x, pNormal->y, pNormal->z),
            Vec2f(pTexCoords->x, pTexCoords->y));

        //if (vertexIdx == 0) {
        //    mesh.m_BBox = BBox3f(Vec3f(mesh.m_Vertices.back().position));
        //}
        //else {
        //    mesh.m_BBox.grow(Vec3f(mesh.m_Vertices.back().position));
        //}
    }

    mesh.m_Triangles.reserve(aimesh->mNumFaces);
    for (size_t triangleIdx = 0; triangleIdx < aimesh->mNumFaces; ++triangleIdx) {
        const aiFace& face = aimesh->mFaces[triangleIdx];
        mesh.m_Triangles.emplace_back(face.mIndices[0], face.mIndices[1], face.mIndices[2]);
    }

    geometry.append(mesh);
}

void loadAssimpScene(const aiScene* aiscene, const std::string& filepath, SceneGeometry& geometry) {
    auto materialOffset = geometry.getMaterialCount();

    fs::path path(filepath);

    //geometry.m_Materials.reserve(materialOffset + aiscene->mNumMaterials);
    for (size_t materialIdx = 0; materialIdx < aiscene->mNumMaterials; ++materialIdx) {
        loadMaterial(aiscene->mMaterials[materialIdx], path.parent_path(), geometry);
    }

    //geometry.m_TriangleMeshs.reserve(geometry.m_TriangleMeshs.size() + aiscene->mNumMeshes);
    for (size_t meshIdx = 0u; meshIdx < aiscene->mNumMeshes; ++meshIdx) {
        loadMesh(aiscene->mMeshes[meshIdx], materialOffset, geometry);
    }
}

SceneGeometry loadModel(const std::string& filepath) {

    Assimp::Importer importer;

    //importer.SetExtraVerbose(true); // TODO: add logger and check for sponza
    const aiScene* aiscene = importer.ReadFile(filepath.c_str(),
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_FlipUVs);
    if (aiscene) {
        std::clog << "Number of meshes = " << aiscene->mNumMeshes << std::endl;
        try {
            SceneGeometry geometry;
            loadAssimpScene(aiscene, filepath, geometry);

            return geometry;
        }
        catch (const std::runtime_error& e) {
            throw std::runtime_error("Assimp loading error on file " + filepath + ": " + e.what());
        }
    }
    else {
        throw std::runtime_error("Assimp loading error on file " + filepath + ": " + importer.GetErrorString());
    }
}