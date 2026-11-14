#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <NovaEngine/Render/Cubemap.h>
#include <NovaEngine/Render/Material.h>
#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Render/Texture.h>

namespace NovaEngine {

// 模型导入结果（V0.2-14：Assimp 单次解析，网格+材质+贴图路径一次拿全）
struct AssetImportResult {
    std::vector<std::shared_ptr<Mesh>> meshes;              // GPU 网格（与子网格一一对应）
    std::vector<Material> materials;                        // 提取的材质
    std::vector<std::filesystem::path> albedoMapPaths;      // 已解析为项目内相对路径；失败为空
};

class AssetDatabase {
public:
    static AssetDatabase& instance();
    static void setProjectRoot(const std::filesystem::path& root);  // 编辑器启动时调用一次
    static std::filesystem::path projectRoot();

    std::shared_ptr<Mesh> getMesh(const std::filesystem::path& relativePath);    // 单网格（缓存未命中自动加载）
    std::vector<std::shared_ptr<Mesh>> loadModel(const std::filesystem::path& relativePath);  // 多子网格模型
    // 模型导入（V0.2-14）：Assimp 只解析一次；网格走既有缓存，材质/贴图路径入旁表缓存
    AssetImportResult importModel(const std::filesystem::path& relativePath);
    std::shared_ptr<Texture> getTexture(const std::filesystem::path& relativePath, bool srgb = false);
    // relativePath 为目录（含 px/nx/py/ny/pz/nz 六图，扩展名 .png/.jpg/.jpeg/.bmp/.tga/.hdr）
    // 或单文件（equirect）；失败返回 nullptr + Logger::warn
    std::shared_ptr<Cubemap> getCubemap(const std::filesystem::path& relativePath);
    void clearCache();       // 菜单 Reload Assets（修改文件后手动刷新）
    void releaseUnused();

private:
    AssetDatabase() = default;
    std::unordered_map<std::string, std::weak_ptr<Mesh>> m_meshCache;      // key = canonical 绝对路径
    std::unordered_map<std::string, std::weak_ptr<Texture>> m_textureCache;
    std::unordered_map<std::string, std::weak_ptr<Cubemap>> m_cubemapCache;
    std::unordered_map<std::string, Material> m_materialCache;             // 子网格键 → 材质（importModel 旁表）
    std::filesystem::path m_projectRoot;
};

}  // namespace NovaEngine
