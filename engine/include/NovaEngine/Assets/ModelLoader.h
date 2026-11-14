#pragma once
#include <filesystem>
#include <vector>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Render/MeshData.h>

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
    // convertAiMesh/extractAiMaterial 为 .cpp 内部自由函数（assimp 类型不进公共头）
};

}  // namespace NovaEngine
