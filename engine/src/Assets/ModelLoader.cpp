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
#include <cmath>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace NovaEngine {

namespace {

// 单顶点最多 4 骨骼影响（§25.7 冻结：boneIds ivec4 / boneWeights vec4）
constexpr int kMaxBoneInfluences = 4;
// 骨骼 UBO 上限（§25.7 冻结：Mesh 骨骼矩阵 UBO 上限 64）
constexpr size_t kMaxBones = 64;

// Assimp 行主序矩阵 → glm 列主序（转置）
glm::mat4 aiToGlm(const aiMatrix4x4& m) {
    glm::mat4 out;
    out[0][0] = m.a1; out[0][1] = m.b1; out[0][2] = m.c1; out[0][3] = m.d1;
    out[1][0] = m.a2; out[1][1] = m.b2; out[1][2] = m.c2; out[1][3] = m.d2;
    out[2][0] = m.a3; out[2][1] = m.b3; out[2][2] = m.c3; out[2][3] = m.d3;
    out[3][0] = m.a4; out[3][1] = m.b4; out[3][2] = m.c4; out[3][3] = m.d4;
    return out;
}

// 递归按名字找节点（骨骼名在整个节点树内唯一）
const aiNode* findNodeByName(const aiNode* node, const std::string& name) {
    if (!node) return nullptr;
    if (node->mName.C_Str() == name) return node;
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        if (const aiNode* found = findNodeByName(node->mChildren[i], name)) return found;
    return nullptr;
}

// 采样辅助：标量 key（Assimp 轨道 key 时间升序）
template <typename KeyT>
const KeyT& lowerKey(const KeyT* keys, unsigned int count, double time) {
    unsigned int lo = 0, hi = count;
    while (lo + 1 < hi) {
        const unsigned int mid = (lo + hi) / 2;
        if (keys[mid].mTime <= time) lo = mid; else hi = mid;
    }
    return keys[lo];
}

glm::vec3 sampleVec3Keys(const aiVectorKey* keys, unsigned int count, double time, bool have) {
    if (!have || count == 0) return glm::vec3(0.0f);
    if (count == 1 || time <= keys[0].mTime) return glm::vec3(keys[0].mValue.x, keys[0].mValue.y, keys[0].mValue.z);
    if (time >= keys[count - 1].mTime)
        return glm::vec3(keys[count - 1].mValue.x, keys[count - 1].mValue.y, keys[count - 1].mValue.z);
    const aiVectorKey& a = lowerKey(keys, count, time);
    const aiVectorKey& b = *(&a + 1);
    const double dt = b.mTime - a.mTime;
    const float t = dt > 0.0 ? static_cast<float>((time - a.mTime) / dt) : 0.0f;
    const glm::vec3 va(a.mValue.x, a.mValue.y, a.mValue.z);
    const glm::vec3 vb(b.mValue.x, b.mValue.y, b.mValue.z);
    return glm::mix(va, vb, t);
}

glm::quat sampleQuatKeys(const aiQuatKey* keys, unsigned int count, double time, bool have) {
    if (!have || count == 0) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (count == 1 || time <= keys[0].mTime)
        return glm::quat(keys[0].mValue.w, keys[0].mValue.x, keys[0].mValue.y, keys[0].mValue.z);
    if (time >= keys[count - 1].mTime)
        return glm::quat(keys[count - 1].mValue.w, keys[count - 1].mValue.x,
                         keys[count - 1].mValue.y, keys[count - 1].mValue.z);
    const aiQuatKey& a = lowerKey(keys, count, time);
    const aiQuatKey& b = *(&a + 1);
    const double dt = b.mTime - a.mTime;
    const float t = dt > 0.0 ? static_cast<float>((time - a.mTime) / dt) : 0.0f;
    const glm::quat qa(a.mValue.w, a.mValue.x, a.mValue.y, a.mValue.z);
    const glm::quat qb(b.mValue.w, b.mValue.x, b.mValue.y, b.mValue.z);
    // slerp 近共线时数值不稳 → nlerp 兜底
    const float d = glm::dot(qa, qb);
    if (std::abs(d) > 0.9995f) return glm::normalize(glm::mix(qa, qb, t));
    return glm::slerp(qa, qb, t);
}

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

// ===== V0.5-9 蒙皮（§25.7，c7）：skinnedVertices / skeleton / clips 填充 =====
// 仅含骨网格（mNumBones > 0）触发；非蒙皮路径零变化。
void fillSkinnedData(const aiScene* scene, const aiMesh* src, MeshData& data) {
    // 1. 蒙皮顶点：复制基础顶点数据（position/normal/uv/tangent），权重槽清零
    data.skinnedVertices.resize(data.vertices.size());
    for (size_t i = 0; i < data.vertices.size(); ++i) {
        SkinnedVertex v{};
        v.position = data.vertices[i].position;
        v.normal = data.vertices[i].normal;
        v.uv = data.vertices[i].uv;
        v.tangent = data.vertices[i].tangent;
        data.skinnedVertices[i] = v;
    }

    // 2. 权重收集：每顶点最多 4 骨骼；超 4 个影响时保留权重最大的 4 个
    for (unsigned int b = 0; b < src->mNumBones; ++b) {
        const aiBone* bone = src->mBones[b];
        for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
            const aiVertexWeight& vw = bone->mWeights[w];
            if (vw.mVertexId >= data.skinnedVertices.size()) continue;  // 畸形索引防御
            SkinnedVertex& v = data.skinnedVertices[vw.mVertexId];
            int slot = -1;
            float minWeight = vw.mWeight;
            for (int s = 0; s < kMaxBoneInfluences; ++s) {
                if (v.boneWeights[s] <= 0.0f) { slot = s; break; }            // 空槽优先
                if (v.boneWeights[s] < minWeight) { slot = s; minWeight = v.boneWeights[s]; }
            }
            if (slot >= 0) {
                v.boneIds[slot] = static_cast<int>(b);
                v.boneWeights[slot] = vw.mWeight;
            }
        }
    }
    // 3. 骨架 + 动画剪辑（整模型一份；每个含骨子网格都附同一份）
    if (ModelLoader::extractSkeleton(scene, src, data.skeleton))
        ModelLoader::extractAnimations(scene, data.clips);

    // 4. 权重归一化；引用被截断骨（≥ 骨架大小，extractSkeleton 已 warn）的槽清零后重归一
    const size_t boneCount = data.skeleton.bones.size();
    for (SkinnedVertex& v : data.skinnedVertices) {
        for (int s = 0; s < kMaxBoneInfluences; ++s)
            if (v.boneIds[s] < 0 || v.boneIds[s] >= static_cast<int>(boneCount)) v.boneWeights[s] = 0.0f;
        const float sum = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
        if (sum > 0.0f) v.boneWeights /= sum;  // 和为 0 = 无骨影响，保持全 0（shader 侧绑定姿势兜底）
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
        MeshData md = convertAiMesh(mesh);
        // V0.5-9 蒙皮（§25.7）：含骨网格填 skinnedVertices/skeleton/clips；非蒙皮零变化
        if (mesh->mNumBones > 0) fillSkinnedData(scene, mesh, md);
        result.meshes.push_back(std::move(md));

        Material material;
        std::filesystem::path albedoPath;
        extractAiMaterial(scene->mMaterials[mesh->mMaterialIndex], material, albedoPath);
        result.materials.push_back(material);
        result.albedoMapPaths.push_back(albedoPath);
    }

    Logger::info("ModelLoader: 导入成功（{} 个子网格）", scene->mNumMeshes);
    return result;
}

// ===== V0.5-9 蒙皮动画（§25.7 冻结声明，c7 实现）=====

bool ModelLoader::extractSkeleton(const aiScene* src, const aiMesh* skinMesh, Skeleton& out) {
    out = Skeleton{};
    if (!src || !skinMesh || !src->mRootNode || skinMesh->mNumBones == 0) return false;

    // 骨骼顺序 = aiMesh::mBones 顺序；骨骼名在节点树内按名匹配（glTF/FBX 通用）
    std::vector<std::string> names;
    names.reserve(skinMesh->mNumBones);
    for (unsigned int i = 0; i < skinMesh->mNumBones; ++i)
        names.push_back(skinMesh->mBones[i]->mName.C_Str());

    out.bones.resize(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        const aiNode* node = findNodeByName(src->mRootNode, names[i]);
        if (!node) {
            Logger::warn("ModelLoader: 骨骼节点 \"{}\" 不在节点树中，骨架提取失败", names[i]);
            out = Skeleton{};
            return false;
        }
        out.bones[i].name = names[i];
        // mOffsetMatrix：网格空间 → 骨骼局部空间（inverse bind）
        out.bones[i].inverseBind = aiToGlm(skinMesh->mBones[i]->mOffsetMatrix);
        // 父 = 祖先链中最近的骨骼名；根骨 → -1
        out.bones[i].parent = -1;
        for (const aiNode* p = node->mParent; p; p = p->mParent) {
            const auto it = std::find(names.begin(), names.end(), std::string(p->mName.C_Str()));
            if (it != names.end()) {
                out.bones[i].parent = static_cast<int>(std::distance(names.begin(), it));
                break;
            }
        }
    }

    // 骨骼数超出 UBO 上限 64（§25.7 冻结）：截断 + 越界骨权重清零（warn 一次）
    if (out.bones.size() > kMaxBones) {
        Logger::warn("ModelLoader: 骨架 {} 骨超出上限 64，超出部分不参与蒙皮（权重清零）", names.size());
        out.bones.resize(kMaxBones);
        for (SkeletonBone& bone : out.bones)
            if (bone.parent >= static_cast<int>(kMaxBones)) bone.parent = -1;
    }
    return true;
}

bool ModelLoader::extractAnimations(const aiScene* src, std::vector<AnimationClip>& out) {
    out.clear();
    if (!src) return false;
    out.reserve(src->mNumAnimations);

    for (unsigned int a = 0; a < src->mNumAnimations; ++a) {
        const aiAnimation* anim = src->mAnimations[a];
        AnimationClip clip;
        clip.name = anim->mName.length > 0 ? anim->mName.C_Str() : ("clip" + std::to_string(a));
        clip.duration = static_cast<float>(anim->mDuration);           // 单位 tick
        clip.ticksPerSecond = anim->mTicksPerSecond > 0.0 ? static_cast<float>(anim->mTicksPerSecond)
                                                          : 1.0f;
        // 只收骨骼通道（aiNodeAnim）；形变通道（aiMeshAnim）v1 无形变目标，跳过
        for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
            const aiNodeAnim* ch = anim->mChannels[c];
            if (!ch) continue;
            // 时间轴 = position/rotation/scaling 三条轨的并集（升序去重）
            std::vector<double> times;
            for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k) times.push_back(ch->mPositionKeys[k].mTime);
            for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k) times.push_back(ch->mRotationKeys[k].mTime);
            for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k) times.push_back(ch->mScalingKeys[k].mTime);
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end()), times.end());
            if (times.empty()) continue;

            AnimChannel channel;
            channel.boneName = ch->mNodeName.C_Str();
            channel.keys.reserve(times.size());
            for (const double t : times) {
                AnimKey key;
                key.time = static_cast<float>(t);
                key.position = sampleVec3Keys(ch->mPositionKeys, ch->mNumPositionKeys, t, true);
                key.rotation = sampleQuatKeys(ch->mRotationKeys, ch->mNumRotationKeys, t, true);
                key.scale = sampleVec3Keys(ch->mScalingKeys, ch->mNumScalingKeys, t, true);
                channel.keys.push_back(key);
            }
            clip.channels.push_back(std::move(channel));
        }
        out.push_back(std::move(clip));
    }
    return true;
}

}  // namespace NovaEngine
