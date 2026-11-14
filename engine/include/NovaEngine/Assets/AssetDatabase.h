#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
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
    void clearCache();       // 菜单 Reload Assets（修改文件后手动刷新）
    void releaseUnused();

private:
    AssetDatabase() = default;
    std::unordered_map<std::string, std::weak_ptr<Mesh>> m_meshCache;      // key = canonical 绝对路径
    std::unordered_map<std::string, std::weak_ptr<Texture>> m_textureCache;
    std::filesystem::path m_projectRoot;
};

}  // namespace NovaEngine
