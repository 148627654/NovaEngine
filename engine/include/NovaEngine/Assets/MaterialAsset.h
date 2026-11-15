#pragma once
#include <filesystem>
#include <memory>
#include <NovaEngine/Render/Material.h>

namespace NovaEngine {

// 材质资产（V0.3-6）：.novamat JSON（与场景内 material 段同构——参数+贴图路径，不内嵌像素）
class MaterialAsset {
public:
    // AssetStream 读取；失败返回 nullptr + Logger::error
    static std::shared_ptr<MaterialAsset> load(const std::filesystem::path& relPath);
    // 只写参数+贴图路径；失败 Logger::error 返回 false
    static bool save(const std::filesystem::path& relPath, const Material& m);
    const Material& material() const;                 // 已解析 GPU 纹理的 Material
    const std::filesystem::path& path() const;

private:
    MaterialAsset() = default;
    Material m_material;
    std::filesystem::path m_path;
};

}  // namespace NovaEngine
