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

// V0.2-14 旁表缓存：importModel 的材质与贴图路径（键与网格缓存同为 baseKey + "#序号"）。
// 冻结头 AssetDatabase.h 无 m_materialCache 成员（不可改头），AssetDatabase 为单例，
// 故实现为文件内 static，语义与成员缓存等价（clearCache 一并清空）。
std::unordered_map<std::string, Material> s_materialCache;
std::unordered_map<std::string, std::filesystem::path> s_albedoPathCache;

// 解析模型引用的贴图路径为项目内相对路径（沿用编辑器 resolveTextureAndPath 的候选逻辑）：
//   绝对路径 → 直接校验；否则 模型目录/贴图路径 → 项目根/贴图路径。
// 存在且位于项目内 → 相对项目根的相对路径；解析失败 → 空路径（纯色）。
std::filesystem::path resolveTexturePath(const std::filesystem::path& modelDir,
                                         const std::filesystem::path& texPath,
                                         const std::filesystem::path& projectRoot) {
    std::vector<std::filesystem::path> candidates;
    if (texPath.is_absolute()) {
        candidates.push_back(texPath);
    } else {
        candidates.push_back(modelDir / texPath);   // 相对模型目录（ModelLoader 原始引用形式）
        candidates.push_back(projectRoot / texPath);  // 相对项目根
    }
    std::error_code ec;
    for (const auto& c : candidates) {
        if (!std::filesystem::exists(c, ec)) continue;
        const std::filesystem::path abs = std::filesystem::absolute(c, ec).lexically_normal();
        const std::filesystem::path rel = std::filesystem::relative(abs, projectRoot, ec);
        const bool inside = !ec && !rel.empty() && *rel.begin() != "..";
        if (inside) return rel.lexically_normal();
    }
    return {};   // 解析失败 → 空路径
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
    // V0.2-14：loadModel 兼容转发 importModel 的 meshes（Assimp 只解析一次，
    // 材质/贴图路径由 importModel 一并取回入旁表缓存，此处只返回网格）
    AssetImportResult imported = importModel(relativePath);
    return std::move(imported.meshes);
}

AssetImportResult AssetDatabase::importModel(const std::filesystem::path& relativePath) {
    AssetImportResult out;
    if (relativePath.empty()) return out;

    // 内置图元不占缓存：单网格 + 默认材质 + 空贴图路径
    if (auto builtin = getBuiltinMesh(relativePath.string())) {
        out.meshes.push_back(builtin);
        out.materials.push_back(Material{});
        out.albedoMapPaths.emplace_back();
        return out;
    }

    const std::filesystem::path abs = m_projectRoot / relativePath;
    const std::string baseKey = cacheKeyFor(abs);

    // 网格：先尝试从既有缓存取回全部子网格（键 = 绝对路径 + "#序号"）
    for (size_t i = 0; i < 1024; ++i) {
        auto it = m_meshCache.find(subMeshKey(baseKey, i));
        if (it == m_meshCache.end()) break;
        if (auto mesh = it->second.lock()) {
            out.meshes.push_back(mesh);
        } else {
            m_meshCache.erase(it);  // 已失效：清掉并停止（后续序号同样视为未缓存）
            break;
        }
    }

    if (!out.meshes.empty()) {
        // 网格缓存命中：材质/贴图路径从旁表缓存取回
        for (size_t i = 0; i < out.meshes.size(); ++i) {
            const std::string key = subMeshKey(baseKey, i);
            auto mit = s_materialCache.find(key);
            out.materials.push_back(mit != s_materialCache.end() ? mit->second : Material{});
            auto pit = s_albedoPathCache.find(key);
            out.albedoMapPaths.push_back(pit != s_albedoPathCache.end()
                                             ? pit->second
                                             : std::filesystem::path());
        }
        return out;
    }

    // 缓存未命中：Assimp 一次解析（网格 + 材质 + 贴图路径一次拿全，V0.2-14）
    ModelImportResult parsed = ModelLoader::load(abs);
    if (parsed.meshes.empty()) return out;  // ModelLoader 已记录错误日志

    out.meshes.reserve(parsed.meshes.size());
    out.materials.reserve(parsed.materials.size());
    out.albedoMapPaths.reserve(parsed.albedoMapPaths.size());

    for (size_t i = 0; i < parsed.meshes.size(); ++i) {
        auto mesh = Mesh::create(parsed.meshes[i]);
        if (!mesh) {
            Logger::error("AssetDatabase: 上传网格失败 [{}]#{}", relativePath.string(), i);
            continue;
        }
        const std::string key = subMeshKey(baseKey, i);
        m_meshCache[key] = mesh;
        out.meshes.push_back(mesh);

        // 旁表缓存：材质 + 贴图路径（相对路径解析失败 → 空路径 = 纯色）
        const Material material =
            i < parsed.materials.size() ? parsed.materials[i] : Material{};
        s_materialCache[key] = material;
        out.materials.push_back(material);

        const std::filesystem::path texRel =
            (i < parsed.albedoMapPaths.size() && !parsed.albedoMapPaths[i].empty())
                ? resolveTexturePath(abs.parent_path(), parsed.albedoMapPaths[i], m_projectRoot)
                : std::filesystem::path();
        s_albedoPathCache[key] = texRel;
        out.albedoMapPaths.push_back(texRel);
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
    // V0.2-14：旁表缓存一并清空（改文件后 Reload Assets 重取材质/贴图路径）
    s_materialCache.clear();
    s_albedoPathCache.clear();
    Logger::info("AssetDatabase: 缓存已清空（Reload Assets）");
}

void AssetDatabase::releaseUnused() {
    std::erase_if(m_meshCache, [](const auto& kv) { return kv.second.expired(); });
    std::erase_if(m_textureCache, [](const auto& kv) { return kv.second.expired(); });
    std::erase_if(m_cubemapCache, [](const auto& kv) { return kv.second.expired(); });
}

}  // namespace NovaEngine
