#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <NovaEngine/Render/Cubemap.h>
#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Render/Texture.h>

namespace NovaEngine {

class AssetDatabase {
public:
    static AssetDatabase& instance();
    static void setProjectRoot(const std::filesystem::path& root);  // 编辑器启动时调用一次
    static std::filesystem::path projectRoot();

    std::shared_ptr<Mesh> getMesh(const std::filesystem::path& relativePath);    // 单网格（缓存未命中自动加载）
    std::vector<std::shared_ptr<Mesh>> loadModel(const std::filesystem::path& relativePath);  // 多子网格模型
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
    std::filesystem::path m_projectRoot;
};

}  // namespace NovaEngine
