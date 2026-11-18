#pragma once
#include <filesystem>
#include <vector>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Render/MeshData.h>

// assimp C 类型前向声明（V0.5-9 冻结声明用；完整定义仅 .cpp 内可见，不污染公共头）
struct aiScene;
struct aiMesh;

namespace NovaEngine {

struct ModelImportResult {
    std::vector<MeshData> meshes;                       // 每个 aiMesh 一个
    std::vector<Material> materials;                    // 与 meshes 一一对应（第 i 个子网格用第 i 个材质）
    std::vector<std::filesystem::path> albedoMapPaths;  // 可为空路径
};

class ModelLoader {
public:
    // 失败时 meshes 为空 + Logger::error
    static ModelImportResult load(const std::filesystem::path& absolutePath);
    // 从内存字节导入（AssetStream/pak 用；extensionHint 如 ".obj"/".gltf" 供 Assimp 定格式）
    static ModelImportResult loadFromMemory(const uint8_t* data, size_t size, const std::string& extensionHint);
    // convertAiMesh/extractAiMaterial 为 .cpp 内部自由函数（assimp 类型不进公共头）

    // ===== V0.5-9 蒙皮动画（§25.7 冻结声明，骨架日 c0 布；实现归 c7 的 ModelLoader.cpp）=====
    static bool extractSkeleton(const aiScene* src, const aiMesh* skinMesh, Skeleton& out);
    static bool extractAnimations(const aiScene* src, std::vector<AnimationClip>& out);   // aiNodeAnim/aiMeshAnim
};

}  // namespace NovaEngine
