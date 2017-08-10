#include "scene/Scene.hpp"

#include <iostream>
#include <filesystem>

#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace fs = std::experimental::filesystem;

namespace c2ba
{

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
    std::vector<SceneGeometry::Vertex> vertices;
    std::vector<SceneGeometry::Triangle> triangles;

#ifdef _DEBUG
    mesh.m_MaterialID = 0;
#else
    const auto m_MaterialID = materialOffset + aimesh->mMaterialIndex;
#endif

    vertices.reserve(aimesh->mNumVertices);
    for (size_t vertexIdx = 0; vertexIdx < aimesh->mNumVertices; ++vertexIdx) {
        const aiVector3D* pPosition = aimesh->HasPositions() ? &aimesh->mVertices[vertexIdx] : &aiZERO;
        const aiVector3D* pNormal = aimesh->HasNormals() ? &aimesh->mNormals[vertexIdx] : &aiZERO;
        const aiVector3D* pTexCoords = aimesh->HasTextureCoords(0) ? &aimesh->mTextureCoords[0][vertexIdx] : &aiZERO;

        vertices.emplace_back(
            float3(pPosition->x, pPosition->y, pPosition->z),
            float3(pNormal->x, pNormal->y, pNormal->z),
            float2(pTexCoords->x, pTexCoords->y));

        //if (vertexIdx == 0) {
        //    mesh.m_BBox = BBox3f(Vec3f(mesh.m_Vertices.back().position));
        //}
        //else {
        //    mesh.m_BBox.grow(Vec3f(mesh.m_Vertices.back().position));
        //}
    }

    triangles.reserve(aimesh->mNumFaces);
    for (size_t triangleIdx = 0; triangleIdx < aimesh->mNumFaces; ++triangleIdx) {
        const aiFace& face = aimesh->mFaces[triangleIdx];
        triangles.emplace_back(face.mIndices[0], face.mIndices[1], face.mIndices[2]);
    }

    geometry.append(vertices.data(), vertices.size(), triangles.data(), triangles.size());
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

}