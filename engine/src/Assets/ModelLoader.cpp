// ModelLoader.cpp —— Assimp 模型导入实现
//
// assimp 类型只在本 .cpp 内部使用，不泄漏到公共头（公共头只暴露 MeshData/Material/路径）。

#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <glm/common.hpp>
#include <glm/vec3.hpp>

namespace NovaEngine {

namespace {

// 转换单个 aiMesh → MeshData（含 AABB 与切线）
MeshData convertAiMesh(const aiMesh* src) {
    MeshData data;
    if (!src) return data;

    data.name = src->mName.C_Str();
    data.vertices.reserve(src->mNumVertices);

    for (unsigned int i = 0; i < src->mNumVertices; ++i) {
        Vertex v{};
        v.position = glm::vec3(src->mVertices[i].x, src->mVertices[i].y, src->mVertices[i].z);
        if (src->HasNormals())
            v.normal = glm::vec3(src->mNormals[i].x, src->mNormals[i].y, src->mNormals[i].z);
        if (src->HasTextureCoords(0))
            v.uv = glm::vec2(src->mTextureCoords[0][i].x, src->mTextureCoords[0][i].y);
        // aiProcess_CalcTangentSpace 已保证有 UV 的网格带有切线
        if (src->HasTangentsAndBitangents())
            v.tangent = glm::vec3(src->mTangents[i].x, src->mTangents[i].y, src->mTangents[i].z);

        data.vertices.push_back(v);
        data.aabb.min = glm::min(data.aabb.min, v.position);
        data.aabb.max = glm::max(data.aabb.max, v.position);
    }
    // 空网格：收缩为原点，避免出现「无限大」包围盒
    if (data.vertices.empty()) {
        data.aabb.min = glm::vec3(0.0f);
        data.aabb.max = glm::vec3(0.0f);
    }

    // 索引：三角化后正常为三角形；非三角形面按扇形兜底处理
    for (unsigned int f = 0; f < src->mNumFaces; ++f) {
        const aiFace& face = src->mFaces[f];
        for (unsigned int k = 0; k + 2 < face.mNumIndices; ++k) {
            data.indices.push_back(face.mIndices[0]);
            data.indices.push_back(face.mIndices[k + 1]);
            data.indices.push_back(face.mIndices[k + 2]);
        }
    }
    return data;
}

// 提取 aiMaterial → Material + 反照率贴图路径
// 贴图路径保持文件内引用的原始形式（相对模型文件所在目录），由调用方负责拼接解析。
void extractAiMaterial(const aiMaterial* src, Material& out, std::filesystem::path& outAlbedoPath) {
    if (!src) return;

    aiColor3D color;
    if (src->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
        out.albedo = glm::vec3(color.r, color.g, color.b);

    // PBR 系数（glTF 经 Assimp 提供这些 key；OBJ/FBX 缺失时保持默认值）
    float metallic = 0.0f;
    if (src->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
        out.metallic = metallic;

    float roughness = 0.0f;
    if (src->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        out.roughness = roughness;
    } else {
        // 传统 Phong shininess 近似换算成 roughness：roughness = 1 - sqrt(shininess/1000)
        float shininess = 0.0f;
        if (src->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
            out.roughness = std::clamp(1.0f - std::sqrt(std::clamp(shininess, 0.0f, 1000.0f) / 1000.0f),
                                       0.05f, 1.0f);
    }

    // 反照率贴图：优先 DIFFUSE，其次 BASE_COLOR（glTF）
    aiString texPath;
    if (src->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) != AI_SUCCESS)
        src->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath);
    if (texPath.length > 0) {
        std::string p = texPath.C_Str();
        std::replace(p.begin(), p.end(), '\\', '/');  // 统一为可移植路径
        outAlbedoPath = std::filesystem::path(p);
    }
}

}  // namespace

ModelImportResult ModelLoader::load(const std::filesystem::path& absolutePath) {
    // 读取收敛到 AssetStream（中文路径安全 + Wave 2 pak 内存读取），解析逻辑转发 loadFromMemory
    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(absolutePath, bytes)) {
        Logger::error("ModelLoader: 读取文件失败 {}", absolutePath.string());
        return {};  // 失败：meshes 为空
    }
    // 解析失败原因由 loadFromMemory 内部记录（含 hint），此处不重复打日志
    return loadFromMemory(bytes.data(), bytes.size(), absolutePath.extension().string());
}

ModelImportResult ModelLoader::loadFromMemory(const uint8_t* data, size_t size,
                                              const std::string& extensionHint) {
    ModelImportResult result;
    if (!data || size == 0) {
        Logger::error("ModelLoader: 输入字节为空，无法导入");
        return result;
    }

    Assimp::Importer importer;
    // 后处理标志（§6.4）：
    //   Triangulate/JoinIdenticalVertices/CalcTangentSpace/GenSmoothNormals/FlipUVs
    // FlipUVs 把 OBJ/FBX 的 UV 翻成 GL 约定，因此 stb 加载纹理时不再翻转（避免双翻转）。
    constexpr unsigned int kPostProcess =
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_FlipUVs;

    // ReadFileFromMemory：hint = 扩展名（如 ".obj"/".gltf"）供 Assimp 定格式
    const aiScene* scene =
        importer.ReadFileFromMemory(data, size, kPostProcess, extensionHint.c_str());
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->HasMeshes()) {
        Logger::error("ModelLoader: 导入失败（hint {}）: {}", extensionHint,
                      importer.GetErrorString());
        return result;  // 失败：meshes 为空
    }

    result.meshes.reserve(scene->mNumMeshes);
    result.materials.reserve(scene->mNumMeshes);
    result.albedoMapPaths.reserve(scene->mNumMeshes);

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        result.meshes.push_back(convertAiMesh(mesh));

        Material material;
        std::filesystem::path albedoPath;
        extractAiMaterial(scene->mMaterials[mesh->mMaterialIndex], material, albedoPath);
        result.materials.push_back(material);
        result.albedoMapPaths.push_back(albedoPath);
    }

    Logger::info("ModelLoader: 导入成功（{} 个子网格）", scene->mNumMeshes);
    return result;
}

}  // namespace NovaEngine
