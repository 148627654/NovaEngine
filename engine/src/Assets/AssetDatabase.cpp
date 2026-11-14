// AssetDatabase.cpp —— 资产注册表实现
//
// 纯内存缓存：unordered_map<canonical 绝对路径, weak_ptr>。
// 同文件只导入一次；releaseUnused 回收无引用资产；不监听 mtime（改文件后手动 Reload Assets）。

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Primitives.h>

#include <array>
#include <string>

namespace NovaEngine {

namespace {

// 缓存键：canonical 绝对路径字符串（不要求文件存在，用 weakly_canonical）
std::string cacheKeyFor(const std::filesystem::path& absolutePath) {
    std::error_code ec;
    return std::filesystem::weakly_canonical(absolutePath, ec).string();
}

// 多子网格模型的缓存键：绝对路径 + "#序号"
std::string subMeshKey(const std::string& baseKey, size_t index) {
    return baseKey + "#" + std::to_string(index);
}

// 编辑器内置图元：MeshRenderer.meshPath 以 "builtin:" 开头时解析。
// 编辑器创建 Cube/Plane 时使用该约定，使图元网格可以随场景序列化往返
// （SceneSerializer 只持久化 meshPath 字符串，反序列化时经 getMesh 重新解析）。
std::shared_ptr<Mesh> getBuiltinMesh(const std::string& path) {
    if (path == "builtin:cube") return Primitives::cube();
    if (path == "builtin:sphere") return Primitives::sphere();
    if (path == "builtin:plane") return Primitives::plane();
    return nullptr;
}

}  // namespace

AssetDatabase& AssetDatabase::instance() {
    static AssetDatabase s_instance;
    return s_instance;
}

void AssetDatabase::setProjectRoot(const std::filesystem::path& root) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(root, ec);
    instance().m_projectRoot = abs.lexically_normal();
}

std::filesystem::path AssetDatabase::projectRoot() {
    return instance().m_projectRoot;
}

std::vector<std::shared_ptr<Mesh>> AssetDatabase::loadModel(const std::filesystem::path& relativePath) {
    std::vector<std::shared_ptr<Mesh>> out;
    if (relativePath.empty()) return out;

    // 内置图元不占缓存
    if (auto builtin = getBuiltinMesh(relativePath.string())) {
        out.push_back(builtin);
        return out;
    }

    const std::filesystem::path abs = m_projectRoot / relativePath;
    const std::string baseKey = cacheKeyFor(abs);

    // 先尝试从缓存取回全部子网格（键 = 绝对路径 + "#序号"）
    for (size_t i = 0; i < 1024; ++i) {
        auto it = m_meshCache.find(subMeshKey(baseKey, i));
        if (it == m_meshCache.end()) break;
        if (auto mesh = it->second.lock()) {
            out.push_back(mesh);
        } else {
            m_meshCache.erase(it);  // 已失效：清掉并停止（后续序号同样视为未缓存）
            break;
        }
    }
    if (!out.empty()) return out;

    // 缓存未命中：从磁盘导入并缓存
    ModelImportResult result = ModelLoader::load(abs);
    if (result.meshes.empty()) return out;  // ModelLoader 已记录错误日志

    for (size_t i = 0; i < result.meshes.size(); ++i) {
        auto mesh = Mesh::create(result.meshes[i]);
        if (mesh) {
            m_meshCache[subMeshKey(baseKey, i)] = mesh;
            out.push_back(mesh);
        } else {
            Logger::error("AssetDatabase: 上传网格失败 [{}]#{}", relativePath.string(), i);
        }
    }
    return out;
}

std::shared_ptr<Mesh> AssetDatabase::getMesh(const std::filesystem::path& relativePath) {
    if (relativePath.empty()) return nullptr;
    if (auto builtin = getBuiltinMesh(relativePath.string())) return builtin;
    auto meshes = loadModel(relativePath);
    return meshes.empty() ? nullptr : meshes.front();
}

std::shared_ptr<Texture> AssetDatabase::getTexture(const std::filesystem::path& relativePath, bool srgb) {
    if (relativePath.empty()) return nullptr;

    const std::filesystem::path abs = m_projectRoot / relativePath;
    const std::string key = cacheKeyFor(abs);

    if (auto it = m_textureCache.find(key); it != m_textureCache.end())
        if (auto tex = it->second.lock())
            return tex;

    auto tex = Texture::createFromFile(abs, srgb);
    if (!tex) {
        Logger::error("AssetDatabase: 加载贴图失败 {}", relativePath.string());
        return nullptr;
    }
    m_textureCache[key] = tex;
    return tex;
}

std::shared_ptr<Cubemap> AssetDatabase::getCubemap(const std::filesystem::path& relativePath) {
    if (relativePath.empty()) return nullptr;

    const std::filesystem::path abs = m_projectRoot / relativePath;
    const std::string key = cacheKeyFor(abs);

    // 缓存命中（weak_ptr 失效时锁失败，走重载）
    if (auto it = m_cubemapCache.find(key); it != m_cubemapCache.end())
        if (auto cubemap = it->second.lock())
            return cubemap;

    // 目录 → 6 面图（固定文件名 px/nx/py/ny/pz/nz）；文件 → equirect
    std::error_code ec;
    std::shared_ptr<Cubemap> cubemap;
    if (std::filesystem::is_directory(abs, ec)) {
        static constexpr std::array<const char*, 6> kBaseNames = {"px", "nx", "py", "ny", "pz", "nz"};
        static constexpr std::array<const char*, 6> kExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr"};
        std::array<std::filesystem::path, 6> faces{};
        bool allFound = true;
        for (size_t i = 0; i < kBaseNames.size(); ++i) {
            bool found = false;
            for (const char* ext : kExtensions) {
                const std::filesystem::path candidate =
                    abs / (std::string(kBaseNames[i]) + ext);
                if (std::filesystem::exists(candidate, ec)) {
                    faces[i] = candidate;
                    found = true;
                    break;
                }
            }
            if (!found) {
                Logger::warn("AssetDatabase: 6 面天空盒目录缺少 {}.* 贴图，跳过 {}",
                             kBaseNames[i], relativePath.string());
                allFound = false;
                break;
            }
        }
        if (allFound)
            cubemap = Cubemap::createFromFaces(faces, /*srgb=*/true);
    } else if (std::filesystem::is_regular_file(abs, ec)) {
        cubemap = Cubemap::createFromEquirect(abs);
    } else {
        Logger::warn("AssetDatabase: 天空盒路径不存在: {}", relativePath.string());
        return nullptr;
    }

    if (!cubemap) return nullptr;   // 失败原因已由 Cubemap 内部日志记录
    m_cubemapCache[key] = cubemap;
    return cubemap;
}

void AssetDatabase::clearCache() {
    m_meshCache.clear();
    m_textureCache.clear();
    m_cubemapCache.clear();
    Logger::info("AssetDatabase: 缓存已清空（Reload Assets）");
}

void AssetDatabase::releaseUnused() {
    std::erase_if(m_meshCache, [](const auto& kv) { return kv.second.expired(); });
    std::erase_if(m_textureCache, [](const auto& kv) { return kv.second.expired(); });
    std::erase_if(m_cubemapCache, [](const auto& kv) { return kv.second.expired(); });
}

}  // namespace NovaEngine
