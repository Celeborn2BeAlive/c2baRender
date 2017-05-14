#include <iostream>
#include <filesystem>
#include <vector>

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

    void append(const TriangleMesh & mesh)
    {
        const auto offset = m_Vertices.size();
        m_Vertices.insert(end(m_Vertices), begin(mesh.m_Vertices), end(mesh.m_Vertices));

        for (const auto & triangle : mesh.m_Triangles)
        {
            m_Triangles.emplace_back(offset + triangle.v0, offset + triangle.v1, offset + triangle.v2);
        }
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

    const RTCDevice embreeDevice = rtcNewDevice();

    SceneGeometry geometry = loadModel(argv[1]);

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

    glEnable(GL_DEPTH_TEST);

    const auto m_program = compileProgram({ m_ShadersRootPath / m_AppName / "forward.vs.glsl", m_ShadersRootPath / m_AppName / "forward.fs.glsl" });
    m_program.use();

    const auto m_uModelViewProjMatrixLocation = glGetUniformLocation(m_program.glId(), "uModelViewProjMatrix");
    const auto m_uModelViewMatrixLocation = glGetUniformLocation(m_program.glId(), "uModelViewMatrix");
    const auto m_uNormalMatrixLocation = glGetUniformLocation(m_program.glId(), "uNormalMatrix");
    
    ViewController m_viewController{ m_pWindow };
    m_viewController.setViewMatrix(glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0)));
    m_viewController.setSpeed(3000.f * 0.1f);

    for (auto iterationCount = 0u; !glfwWindowShouldClose(m_pWindow); ++iterationCount)
    {
        auto seconds = glfwGetTime();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const auto projMatrix = glm::perspective(glm::radians(70.f), float(m_nWindowWidth) / m_nWindowHeight, 0.01f * 3000.f, 3000.f);
        const auto viewMatrix = m_viewController.getViewMatrix();

        const auto modelMatrix = glm::mat4();

        const auto mvMatrix = viewMatrix * modelMatrix;
        const auto mvpMatrix = projMatrix * mvMatrix;
        const auto normalMatrix = glm::transpose(glm::inverse(mvMatrix));

        glUniformMatrix4fv(m_uModelViewProjMatrixLocation, 1, GL_FALSE, glm::value_ptr(mvpMatrix));
        glUniformMatrix4fv(m_uModelViewMatrixLocation, 1, GL_FALSE, glm::value_ptr(mvMatrix));
        glUniformMatrix4fv(m_uNormalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));

        glBindVertexArray(m_SceneVAO);

        glDrawElements(GL_TRIANGLES, geometry.m_Triangles.size() * 3, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);

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
        if (!guiHasFocus && m_viewController.update(float(ellapsedTime))) {
            // clear
        }
    }

    ImGui_ImplGlfwGL3_Shutdown();
    glfwTerminate();

    rtcDeleteDevice(embreeDevice);

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