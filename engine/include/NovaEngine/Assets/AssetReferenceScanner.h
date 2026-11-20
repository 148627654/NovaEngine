#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace NovaEngine {

// ===== V1.0-7 资产引用完整性（§26.4 冻结接口，2026-11-20 骨架日布）=====
// 头文件签名冻结：实现归 cA（engine/src/Assets/AssetReferenceScanner.cpp）；变更须报 Architect 重议。

struct AssetReference {
    std::string assetPath;    // 项目相对路径（被引用的资产）
    std::string kind;         // "mesh"/"texture"/"materialAsset"/"cubemap"/"audioClip"/"prefab"/"scene"/"font"/"terrainHeightmap"/"lut"/"shaderGraph"
    std::string fromFile;     // 引用方文件（场景/材质/预制体/build 列表路径）
};

class AssetReferenceScanner {
public:
    static std::vector<AssetReference> scanScene(const std::filesystem::path& scenePath);
    static std::vector<AssetReference> scanMaterial(const std::filesystem::path& matPath);
    static std::vector<AssetReference> scanPrefab(const std::filesystem::path& prefabPath);  // 递归嵌套，深度上限 8 防环（§22 既有纪律）
    static std::vector<AssetReference> scanProject();   // assets/ 全量：场景+.novamat+.novaprefab+build.novabuild
    // 移动资产：磁盘移动 + 全项目引用重写（场景 JSON/.novamat/.novaprefab/build.novabuild）
    // 重写保序无损（nlohmann::json 读改写，键序 std::map 稳定；浮点精度既有「无损重排」备案）；
    // 返回被重写的引用数；移动失败/目标已存在 → -1 + Logger::error
    static int moveAsset(const std::filesystem::path& from, const std::filesystem::path& to);
    // 存在性判定经 AssetStream（pak 命中优先、磁盘回退——与既有资产读取语义一致）
};

}  // namespace NovaEngine
