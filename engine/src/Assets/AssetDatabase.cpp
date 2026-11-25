// AssetDatabase.cpp —— 资产注册表实现
//
// 纯内存缓存：unordered_map<canonical 绝对路径, weak_ptr>。
// 同文件只导入一次；releaseUnused 回收无引用资产；不监听 mtime（改文件后手动 Reload Assets）。
//
// V0.3-7 二进制缓存（assets/.novacache，详见 BinaryCache.cpp）：
//   getTexture：源字节 + meta 文本 → key；命中直接上传缓存像素，未命中解码 + 写缓存
//   importModel：key = 源文件字节；命中跳过 Assimp 直接 Mesh::create；未命中解析后逐子网格写缓存
//   缓存损坏由 BinaryCache 自行删除重导，本文件无需感知。

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/BinaryCache.h>
#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Assets/TextureLoader.h>
#include <NovaEngine/Assets/TextureMeta.h>
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

// V0.2-14 旁表缓存：importModel 的贴图路径（键与网格缓存同为 baseKey + "#序号"）。
// 冻结头 AssetDatabase.h 无该表成员（材质表已由 Wave 2 解冻头加入 m_materialCache），
// AssetDatabase 为单例，故实现为文件内 static，语义与成员缓存等价（clearCache 一并清空）。
std::unordered_map<std::string, std::filesystem::path> s_albedoPathCache;

// V1.0-13 旁表缓存：导入成功时记录的顶点+索引字节（键同网格缓存，§28.1 memorySnapshot 校准）。
// 冻结头无该表成员 → 文件内 static（clearCache/releaseUnused 与网格缓存联动清空）。
std::unordered_map<std::string, size_t> s_meshBytesCache;

// MeshData 的 GPU 上传字节估算：顶点（含蒙皮顶点）+ 索引（§26.2「顶点+索引字节」）
size_t meshDataBytes(const MeshData& data) {
    return data.vertices.size() * sizeof(Vertex) +
           data.skinnedVertices.size() * sizeof(SkinnedVertex) +
           data.indices.size() * sizeof(uint32_t);
}

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

// 6 面天空盒文件名与扩展名（getCubemap 目录探测共用）
constexpr std::array<const char*, 6> kCubeBaseNames = {"px", "nx", "py", "ny", "pz", "nz"};
constexpr std::array<const char*, 6> kCubeExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr"};

// 相对路径 → 绝对路径解析（修复：Player 裸盘模式模型/贴图加载失败）
// 常规锚点 = 项目根；但 Player 的项目根 = exe 目录，裸盘开发模式从项目根运行时
// 资产实际位于 cwd（场景经 cwd 加载）。因此：
//   pak 已挂载 → 恒用项目根锚点（pak 键按项目根降维；cwd 回退会绕过 pak 直读磁盘）；
//   磁盘上项目根锚点存在 → 用之（编辑器/常规流程不变）；
//   否则 cwd 锚点存在 → 用之（裸盘回退）；
//   都不存在 → 保留项目根锚点（后续 AssetStream 报错路径不变）。
std::filesystem::path resolveAssetAbs(const std::filesystem::path& relativePath) {
    const std::filesystem::path projAbs = AssetDatabase::projectRoot() / relativePath;
    if (AssetStream::pakMounted()) return projAbs;
    std::error_code ec;
    if (std::filesystem::exists(projAbs, ec)) return projAbs;
    std::error_code cwdEc;
    const std::filesystem::path cwdAbs = std::filesystem::current_path(cwdEc) / relativePath;
    if (!cwdEc && std::filesystem::exists(cwdAbs, cwdEc)) return cwdAbs;
    return projAbs;
}

// ===== V1.0-6 压缩格式决策（§26.3）=====

constexpr int kCompAutoNeedPixels = -2;   // Auto：需解码像素做内容判定（alpha 检测）
constexpr int kCompNone = -1;             // 不压缩（未压缩 RGBA8 路径）

const char* compressionFormatName(int format) {
    switch (format) {
        case 0: return "BC7";
        case 1: return "BC1";
        case 2: return "BC5";
        default: return "未压缩";
    }
}

// warn 一次纪律（§26.3「warn 一次」；进程内每种原因只打一次）
bool s_warnedCapsUnavailable = false;    // GPU 完全无压缩支持（含模拟不支持注入场景）
bool s_warnedFormatFallback = false;     // 所选格式不支持 → 逐级回退
bool s_warnedEncoderUnavailable = false; // bc7enc 未接入 / 编码失败

// 返回最终压缩格式（0=BC7 1=BC1 2=BC5）；kCompNone = 不压缩；kCompAutoNeedPixels = Auto 需先解码
int chooseCompressionFormat(const TextureImportSettings& s, bool srgbInternal,
                            const uint8_t* rgba8 = nullptr, size_t pixelCount = 0) {
    if (s.compression == TextureCompression::None) return kCompNone;   // 用户显式关闭：零警告

    const GpuCompressionCaps caps = TextureLoader::queryCompressionCaps();
    const bool anyCaps = caps.bc7 || caps.bc1 || caps.bc5;
    if (!anyCaps) {
        if (!s_warnedCapsUnavailable) {
            s_warnedCapsUnavailable = true;
            Logger::warn("AssetDatabase: GPU 不支持贴图压缩（BC7/BC1/BC5 均不可用），回退未压缩 RGBA8");
        }
        return kCompNone;
    }

    int requested;
    switch (s.compression) {
        case TextureCompression::BC1: requested = 1; break;
        case TextureCompression::BC7: requested = 0; break;
        case TextureCompression::BC5: requested = 2; break;
        case TextureCompression::Auto:
            if (!rgba8) return kCompAutoNeedPixels;   // 调用方先解码再复判
            // 自动策略：法线贴图 → BC5；含有效 alpha → BC7；其余 → BC1（§26.3）
            if (s.isNormalMap) requested = 2;
            else if (TextureLoader::hasAlpha(rgba8, pixelCount)) requested = 0;
            else requested = 1;
            break;
        default: return kCompNone;   // None（上方已拦截，防御）
    }

    // 逐级回退：所选 → BC7 → BC1（去重）→ 未压缩 RGBA8
    const int chain[3] = {requested, 0, 1};
    for (int c : chain) {
        if (!caps.supports(c, srgbInternal)) continue;
        if (c != requested && !s_warnedFormatFallback) {
            s_warnedFormatFallback = true;
            Logger::warn("AssetDatabase: 贴图压缩格式 {} 不支持，回退 {}",
                         compressionFormatName(requested), compressionFormatName(c));
        }
        return c;
    }
    if (!s_warnedFormatFallback) {
        s_warnedFormatFallback = true;
        Logger::warn("AssetDatabase: 贴图压缩格式 {} 不支持，回退未压缩 RGBA8",
                     compressionFormatName(requested));
    }
    return kCompNone;
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

    const std::filesystem::path abs = resolveAssetAbs(relativePath);
    const std::string baseKey = cacheKeyFor(abs);

    // 1. 内存网格缓存：先尝试取回全部子网格（键 = 绝对路径 + "#序号"）
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
            auto mit = m_materialCache.find(key);
            out.materials.push_back(mit != m_materialCache.end() ? mit->second : Material{});
            auto pit = s_albedoPathCache.find(key);
            out.albedoMapPaths.push_back(pit != s_albedoPathCache.end()
                                             ? pit->second
                                             : std::filesystem::path());
        }
        return out;
    }

    // 2. 二进制缓存（V0.3-7）：key = 源文件字节哈希。源字节经 AssetStream 读取
    //    （pak 挂载时走内存；磁盘中文路径安全），Assimp 解析复用同一份字节（免二次读盘）。
    std::vector<uint8_t> srcBytes;
    if (!AssetStream::readAllBytes(abs, srcBytes)) {
        Logger::error("AssetDatabase: 读取模型文件失败 {}", relativePath.string());
        return out;
    }
    const std::string binKey = BinaryCache::makeKey(srcBytes);

    std::vector<MeshData> cachedDatas;
    for (size_t i = 0; i < 1024; ++i) {
        MeshData data;
        if (!BinaryCache::loadMeshData(binKey + "#" + std::to_string(i), data)) break;
        cachedDatas.push_back(std::move(data));
    }

    // 二进制缓存命中条件（Reviewer P1-2/P2-2）：材质旁表必须与本模型全部子网格键
    // 一一对应且数量一致。旁表为空（新进程）或缓存被撕裂（子网格缺失/数量不符）时
    // 不得静默丢材质/丢子网格——一律回退 Assimp 重解析，并在下方逐子网格覆写
    // saveMeshData 补全缓存。
    const std::string keyPrefix = baseKey + "#";
    size_t sideTableCount = 0;
    for (const auto& kv : m_materialCache)
        if (kv.first.starts_with(keyPrefix)) ++sideTableCount;
    bool cacheUsable = !cachedDatas.empty() && sideTableCount == cachedDatas.size();
    for (size_t i = 0; cacheUsable && i < cachedDatas.size(); ++i) {
        if (!m_materialCache.contains(subMeshKey(baseKey, i))) cacheUsable = false;
    }

    if (cacheUsable) {
        // 命中：跳过 Assimp 直接 Mesh::create；材质/贴图路径旁表已确认完整
        for (size_t i = 0; i < cachedDatas.size(); ++i) {
            auto mesh = Mesh::create(cachedDatas[i]);
            if (!mesh) {
                Logger::error("AssetDatabase: 上传网格失败 [{}]#{}", relativePath.string(), i);
                continue;
            }
            const std::string key = subMeshKey(baseKey, i);
            m_meshCache[key] = mesh;
            s_meshBytesCache[key] = meshDataBytes(cachedDatas[i]);   // V1.0-13：字节旁表
            out.meshes.push_back(mesh);
            auto mit = m_materialCache.find(key);
            out.materials.push_back(mit != m_materialCache.end() ? mit->second : Material{});
            auto pit = s_albedoPathCache.find(key);
            out.albedoMapPaths.push_back(pit != s_albedoPathCache.end()
                                             ? pit->second
                                             : std::filesystem::path());
        }
        return out;
    }

    // 3. 未命中（或旁表不完整/缓存撕裂）：Assimp 一次解析（网格 + 材质 + 贴图路径一次拿全，
    //    V0.2-14）+ 逐子网格覆写二进制缓存（补全撕裂缓存）
    ModelImportResult parsed =
        ModelLoader::loadFromMemory(srcBytes.data(), srcBytes.size(), abs.extension().string());
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
        s_meshBytesCache[key] = meshDataBytes(parsed.meshes[i]);   // V1.0-13：字节旁表
        out.meshes.push_back(mesh);

        // 二进制缓存：逐子网格落盘（下次启动免 Assimp）
        BinaryCache::saveMeshData(binKey + "#" + std::to_string(i), parsed.meshes[i]);

        // 旁表缓存：材质 + 贴图路径（相对路径解析失败 → 空路径 = 纯色）
        const Material material =
            i < parsed.materials.size() ? parsed.materials[i] : Material{};
        m_materialCache[key] = material;
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

    const std::filesystem::path abs = resolveAssetAbs(relativePath);
    // 缓存键加 sRGB 维度（Reviewer P1-1a）：同一贴图按 sRGB/线性分别缓存，
    // 消除「先加载者胜出」的格式毒化（albedo 按 sRGB 与法线按线性各持各的实例）
    const std::string texKey = cacheKeyFor(abs) + (srgb ? "#srgb" : "#linear");

    if (auto it = m_textureCache.find(texKey); it != m_textureCache.end())
        if (auto tex = it->second.lock())
            return tex;

    // 源文件字节（AssetStream：pak 挂载自动走内存，未挂载磁盘中文路径安全）
    std::vector<uint8_t> srcBytes;
    if (!AssetStream::readAllBytes(abs, srcBytes)) {
        Logger::error("AssetDatabase: 加载贴图失败（读取）{}", relativePath.string());
        return nullptr;
    }

    // .novameta 导入设置（缺失自动生成默认落盘；损坏回退默认 + warn）
    bool metaExisted = false;
    const TextureImportSettings settings = TextureMeta::loadOrCreate(abs, metaExisted);

    // 二进制缓存键 = 源字节 + meta 文本（改 meta → 缓存 hash 失效，裁定 13）
    const std::string metaText = TextureMeta::serialize(settings);
    const std::string binKey = BinaryCache::makeKey(srcBytes, metaText);

    // sRGB 内部格式仅当 meta 声明 sRGB 且调用方请求 srgb（NormalMap/Linear 一律线性）
    const bool srgbInternal =
        (settings.colorSpace == TextureImportSettings::ColorSpace::Srgb) && srgb;

    // 解码 RGBA8（RGBA8 缓存优先——解码缓存与压缩缓存并存；未命中完整解码 + 写缓存）
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    auto decodePixels = [&]() -> bool {
        if (BinaryCache::loadTextureRgba8(binKey, w, h, pixels)) return true;
        if (!TextureLoader::loadRgba8(abs, w, h, pixels)) {
            Logger::error("AssetDatabase: 加载贴图失败（解码）{}", relativePath.string());
            return false;
        }
        BinaryCache::saveTextureRgba8(binKey, w, h, pixels);
        return true;
    };

    // ---- V1.0-6 压缩路径（§26.3）：meta compression != None 且 GPU 支持所选格式 ----
    int compFormat = chooseCompressionFormat(settings, srgbInternal);
    if (compFormat == kCompAutoNeedPixels) {
        // Auto：先解码做内容判定（alpha 检测），再复判
        if (!decodePixels()) return nullptr;
        compFormat = chooseCompressionFormat(settings, srgbInternal, pixels.data(),
                                             static_cast<size_t>(w) * static_cast<size_t>(h));
    }
    if (compFormat >= 0) {
        // 压缩缓存键 = 源字节 + meta 文本 + 最终格式 + GPU 能力标志
        //（改 meta compression / GPU 能力变化 → 键变自动重导）
        const GpuCompressionCaps caps = TextureLoader::queryCompressionCaps();
        const std::string compKey = BinaryCache::makeKey(
            srcBytes, metaText + ";comp=" + std::to_string(compFormat) + ";caps=" + caps.keyText());

        int cw = 0, ch = 0, cfmt = -1;
        std::vector<uint8_t> blocks;
        // 显式格式路径可先查压缩缓存（命中免解码）；Auto 路径 w/h 已解码，顺带交叉校验
        bool cacheHit =
            BinaryCache::loadTextureCompressed(compKey, cfmt, cw, ch, blocks) &&
            cfmt == compFormat;
        if (cacheHit && w > 0 && (cw != w || ch != h))
            cacheHit = false;   // 防御：与解码尺寸不符（键含格式，理论不可能）→ 重编码
        if (!cacheHit) {
            // 未命中：先解码（Auto 路径上一步已解码则复用），再离线编码（bc7enc 未接入 → 失败回退）
            if (w == 0 || h == 0) {
                if (!decodePixels()) return nullptr;
            }
            const int mips = TextureLoader::encodeCompressed(pixels.data(), w, h, compFormat,
                                                             settings.generateMipmaps, blocks);
            if (mips <= 0) {
                if (!s_warnedEncoderUnavailable) {
                    s_warnedEncoderUnavailable = true;
#ifdef NOVA_HAS_BC7ENC
                    Logger::warn("AssetDatabase: 贴图压缩编码失败（{}），回退未压缩 RGBA8: {}",
                                 compressionFormatName(compFormat), relativePath.string());
#else
                    Logger::warn("AssetDatabase: 压缩库未接入（NOVA_HAS_BC7ENC 未定义），回退未压缩 RGBA8: {}",
                                 relativePath.string());
#endif
                }
                // 回退既有未压缩路径（pixels 已解码、RGBA8 缓存已由 decodePixels 落盘）
                auto tex = Texture::createFromPixels(pixels.data(), w, h, srgbInternal,
                                                     settings.wrapMode, settings.filterMode,
                                                     settings.generateMipmaps);
                if (!tex) {
                    Logger::error("AssetDatabase: 上传贴图失败 {}", relativePath.string());
                    return nullptr;
                }
                m_textureCache[texKey] = tex;
                return tex;
            }
            cw = w;
            ch = h;
            cfmt = compFormat;
            BinaryCache::saveTextureCompressed(compKey, cfmt, cw, ch, blocks);
        }

        auto tex = Texture::createFromCompressed(blocks.data(), blocks.size(), cw, ch, cfmt,
                                                 srgbInternal, settings.wrapMode,
                                                 settings.filterMode, settings.generateMipmaps);
        if (!tex) {
            Logger::error("AssetDatabase: 上传压缩贴图失败 {}", relativePath.string());
            return nullptr;
        }
        // 导入日志 before/after 字节（§26.3「导入日志逐贴图 before/after 字节」）
        Logger::info("AssetDatabase: 贴图压缩 {}（{}x{}，{}）：RGBA8 {} B -> {} B（{} 级 mip）",
                     relativePath.string(), cw, ch, compressionFormatName(cfmt),
                     static_cast<size_t>(cw) * static_cast<size_t>(ch) * 4u, blocks.size(),
                     settings.generateMipmaps ? TextureLoader::mipLevels(cw, ch) : 1);
        m_textureCache[texKey] = tex;
        return tex;
    }

    // ---- 既有未压缩 RGBA8 路径（meta compression=None / GPU 不支持 / 无可用格式时零变化）----
    if (!BinaryCache::loadTextureRgba8(binKey, w, h, pixels)) {
        // 未命中：完整解码 + 写缓存（损坏缓存已被 BinaryCache 删除）
        if (!TextureLoader::loadRgba8(abs, w, h, pixels)) {
            Logger::error("AssetDatabase: 加载贴图失败（解码）{}", relativePath.string());
            return nullptr;
        }
        BinaryCache::saveTextureRgba8(binKey, w, h, pixels);
    }

    // 命中直接用缓存像素（宽高来自缓存）；采样参数按 meta 消费
    auto tex = Texture::createFromPixels(pixels.data(), w, h, srgbInternal, settings.wrapMode,
                                         settings.filterMode, settings.generateMipmaps);
    if (!tex) {
        Logger::error("AssetDatabase: 上传贴图失败 {}", relativePath.string());
        return nullptr;
    }
    m_textureCache[texKey] = tex;
    return tex;
}

std::shared_ptr<Cubemap> AssetDatabase::getCubemap(const std::filesystem::path& relativePath) {
    if (relativePath.empty()) return nullptr;

    const std::filesystem::path abs = resolveAssetAbs(relativePath);
    const std::string key = cacheKeyFor(abs);

    // 缓存命中（强引用：天空盒每帧消费，不再每帧重建转换）
    if (auto it = m_cubemapCache.find(key); it != m_cubemapCache.end())
        return it->second;

    // 目录 → 6 面图（固定文件名 px/nx/py/ny/pz/nz）；文件 → equirect。
    // 目录判定：磁盘 is_directory；pak 挂载时目录不落盘 → 按「任一面文件存在」探测。
    // 探测一律传绝对路径（Reviewer P2-1）：AssetStream 内部对绝对路径相对项目根降维成
    // pak 键（pak 场景成立），磁盘检查不依赖 CWD——与 resolveAssetAbs 的读取锚点一致。
    std::error_code ec;
    std::shared_ptr<Cubemap> cubemap;
    bool dirCase = std::filesystem::is_directory(abs, ec);
    if (!dirCase) {
        for (const char* base : kCubeBaseNames) {
            for (const char* ext : kCubeExtensions) {
                if (AssetStream::exists(abs / (std::string(base) + ext))) {
                    dirCase = true;
                    break;
                }
            }
            if (dirCase) break;
        }
    }

    if (dirCase) {
        std::array<std::filesystem::path, 6> faces{};
        bool allFound = true;
        for (size_t i = 0; i < kCubeBaseNames.size(); ++i) {
            bool found = false;
            for (const char* ext : kCubeExtensions) {
                const std::string faceName = std::string(kCubeBaseNames[i]) + ext;
                if (AssetStream::exists(abs / faceName)) {
                    // 绝对路径交给 Cubemap（内部 AssetStream 读取，pak 自动相对化降维）
                    faces[i] = abs / faceName;
                    found = true;
                    break;
                }
            }
            if (!found) {
                Logger::warn("AssetDatabase: 6 面天空盒目录缺少 {}.* 贴图，跳过 {}",
                             kCubeBaseNames[i], relativePath.string());
                allFound = false;
                break;
            }
        }
        if (allFound)
            cubemap = Cubemap::createFromFaces(faces, /*srgb=*/true);
    } else if (std::filesystem::is_regular_file(abs, ec) || AssetStream::exists(abs)) {
        // equirect：equirect HDR 解码走二进制缓存 RGBFloat（Cubemap 内部消费）
        cubemap = Cubemap::createFromEquirect(abs);
    } else {
        Logger::warn("AssetDatabase: 天空盒路径不存在: {}", relativePath.string());
        return nullptr;
    }

    if (!cubemap) return nullptr;   // 失败原因已由 Cubemap 内部日志记录
    m_cubemapCache[key] = cubemap;
    return cubemap;
}

std::shared_ptr<MaterialAsset> AssetDatabase::getMaterialAsset(
    const std::filesystem::path& relativePath) {
    if (relativePath.empty()) return nullptr;

    const std::filesystem::path abs = resolveAssetAbs(relativePath);
    const std::string key = cacheKeyFor(abs);

    // weak_ptr 缓存命中（失效时锁失败 → 清掉重载）
    if (auto it = m_materialAssetCache.find(key); it != m_materialAssetCache.end()) {
        if (auto asset = it->second.lock())
            return asset;
        m_materialAssetCache.erase(it);
    }

    // 未命中 → MaterialAsset::load（AssetStream 读取 + 贴图解析，细节失败原因已由其记录）
    auto asset = MaterialAsset::load(relativePath);
    if (!asset) {
        Logger::warn("AssetDatabase: 材质资产加载失败 {}", relativePath.string());
        return nullptr;
    }
    m_materialAssetCache[key] = asset;
    return asset;
}

void AssetDatabase::reloadMaterialAsset(const std::filesystem::path& relativePath) {
    // 编辑器保存 .novamat 后刷新：清该路径缓存键 → 下次 getMaterialAsset 重新加载
    //（引用物体由编辑器侧重新解析 materialAsset 指针，下一帧提交即变色）
    if (relativePath.empty()) return;
    const std::filesystem::path abs = resolveAssetAbs(relativePath);
    m_materialAssetCache.erase(cacheKeyFor(abs));
}

std::shared_ptr<AudioClip> AssetDatabase::getAudioClip(const std::filesystem::path& relativePath) {
    // 音频资产（V0.3-2）：weak_ptr 缓存（同文件只解码一次）；解码失败 nullptr + Logger::warn
    if (relativePath.empty()) return nullptr;

    const std::filesystem::path abs = resolveAssetAbs(relativePath);
    const std::string key = cacheKeyFor(abs);

    // 缓存命中（weak_ptr 失效时锁失败 → 清掉重载）
    if (auto it = m_audioClipCache.find(key); it != m_audioClipCache.end()) {
        if (auto clip = it->second.lock())
            return clip;
        m_audioClipCache.erase(it);
    }

    // 未命中 → AudioClip::load（AssetStream 读取 + miniaudio 全量解码，细节失败原因已由其记录）
    auto clip = AudioClip::load(relativePath);
    if (!clip) {
        Logger::warn("AssetDatabase: 音频资产加载失败 {}", relativePath.string());
        return nullptr;
    }
    m_audioClipCache[key] = clip;
    return clip;
}

void AssetDatabase::clearCache() {
    m_meshCache.clear();
    m_textureCache.clear();
    m_cubemapCache.clear();
    m_materialAssetCache.clear();
    m_audioClipCache.clear();   // V0.3-2：音频缓存随 Reload Assets 一并清空
    // 旁表缓存一并清空（改文件后 Reload Assets 重取材质/贴图路径/网格字节）
    m_materialCache.clear();
    s_albedoPathCache.clear();
    s_meshBytesCache.clear();
    Logger::info("AssetDatabase: 缓存已清空（Reload Assets）");
}

void AssetDatabase::releaseUnused() {
    // V1.0-13（KNOWN-ISSUES P3-7 修复，§28.1）：网格缓存清扫联动旁表——
    // 旁表键与网格缓存同构（baseKey#序号），网格弱引用失效的条目一并删除，
    // 防长跑（反复导入不同模型）旁表单调增长。此前旁表仅 clearCache 可清。
    for (auto it = m_meshCache.begin(); it != m_meshCache.end();) {
        if (it->second.expired()) {
            const std::string key = it->first;
            it = m_meshCache.erase(it);
            m_materialCache.erase(key);
            s_albedoPathCache.erase(key);
            s_meshBytesCache.erase(key);
        } else {
            ++it;
        }
    }
    // 防御双保险：旁表残留孤儿条目（理论上与网格缓存逐键配对，此处兜底全清）
    std::erase_if(m_materialCache, [&](const auto& kv) { return !m_meshCache.contains(kv.first); });
    std::erase_if(s_albedoPathCache,
                  [&](const auto& kv) { return !m_meshCache.contains(kv.first); });
    std::erase_if(s_meshBytesCache,
                  [&](const auto& kv) { return !m_meshCache.contains(kv.first); });
    std::erase_if(m_textureCache, [](const auto& kv) { return kv.second.expired(); });
    // cubemap 为强引用缓存（2026-11-19）：releaseUnused 不清——由 clearCache 显式清空
    std::erase_if(m_materialAssetCache, [](const auto& kv) { return kv.second.expired(); });
    std::erase_if(m_audioClipCache, [](const auto& kv) { return kv.second.expired(); });
}

// ===== V1.0 波 1 骨架日冻结（§26.0 冻结项 4）：实现归 cG =====
MemorySnapshot AssetDatabase::memorySnapshot() {
    MemorySnapshot snap;
    AssetDatabase& db = instance();
    // 贴图：sizeBytes 汇总（压缩 = 块字节；未压缩 = w*h*4，§26.3）
    for (const auto& [key, weak] : db.m_textureCache)
        if (auto tex = weak.lock())
            snap.textureBytes += tex->sizeBytes();
    // 立方体贴图：估算 6 面 × faceSize² × 8B/px（GL_RGB16F 6B/px + 完整 mip 链 ≈ ×1.33）
    for (const auto& [key, cube] : db.m_cubemapCache) {
        const size_t facePx =
            static_cast<size_t>(cube->faceSize()) * static_cast<size_t>(cube->faceSize());
        snap.cubemapBytes += facePx * 6u * 8u;
    }
    // 材质资产：存活计数
    for (const auto& [key, weak] : db.m_materialAssetCache)
        if (weak.lock()) ++snap.materialAssetCount;
    // ===== V1.0-13 收口校准（§28.1）=====
    // 网格：存活条目按导入时记录的顶点+索引字节汇总（内置图元不进 m_meshCache 不计入——
    // 静态单例非资产缓存语义；字节 = MeshData 上传前统计，与 GPU VBO/EBO 上传一致）
    for (const auto& [key, weak] : db.m_meshCache) {
        if (!weak.lock())
            continue;
        const auto it = s_meshBytesCache.find(key);
        if (it != s_meshBytesCache.end())
            snap.meshVertexBytes += it->second;
    }
    // 音频：解码 PCM 字节（f32 样本全量；stream=true 流式源不进缓存不计入，§26.2）
    for (const auto& [key, weak] : db.m_audioClipCache)
        if (auto clip = weak.lock())
            snap.audioBytes += clip->pcmF32().size() * sizeof(float);
    // objectCount/componentCount/loadedSceneCount 为活动场景统计，由 Profiler::memorySnapshot
    // 转发层填充（§26.2）；loadedSceneCount 波 1 恒 1 占位——V1.0-2 additive 落地后
    // 应改为实际已加载场景数，改动落在 Profiler.cpp（本文件无该统计的数据源）
    return snap;
}

}  // namespace NovaEngine
