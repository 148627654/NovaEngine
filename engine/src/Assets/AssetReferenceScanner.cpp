// AssetReferenceScanner.cpp —— 资产引用完整性扫描与移动重链（V1.0-7，§26.4）
//
// 冻结头（AssetReferenceScanner.h）实现。四类入口：
//   scanScene/scanMaterial/scanPrefab/scanProject —— 只读扫描，产出 AssetReference 清单
//   moveAsset —— 磁盘移动 + 全项目引用重写（返回重写引用数；失败/目标已存在 → -1）
//
// 覆盖形态清单（实现前先摸清三类资产引用形态，与 §26.4 契约一致并补齐既有组件字段）：
//   场景 JSON（objects[].components[] 按 type 分发）：
//     MeshRenderer: mesh（跳过 "builtin:" 前缀）→ "mesh"；
//       material{albedoMap,normalMap,metallicMap,roughnessMap,emissionMap} → "texture"；
//       material.materialAsset → "materialAsset"（V0.3-6 双轨）
//     SkeletalMeshRenderer: mesh → "mesh"
//     LODGroup: lods[].mesh → "mesh"
//     MeshCollider: meshPath → "mesh"
//     ParticleSystem: texturePath → "texture"；SpriteRenderer: texturePath → "texture"
//     Image: spritePath → "texture"；Slider: background/fill/handleSpritePath → "texture"
//     Terrain: heightmapPath → "terrainHeightmap"；splatmapPath → "texture"；
//       layers[].{albedo,normal} → "texture"
//     AudioSource: clipPath → "audioClip"
//     PrefabInstance: prefabPath → "prefab"（并递归扫描该 .novaprefab，深度上限 8 防环）
//     Text: fontPath → "font"
//   场景 environment.skyCubemapPath → "cubemap"
//   .novamat: material{albedoMap,normalMap,metallicMap,roughnessMap,emissionMap} → "texture"
//     （materialAsset 键防御性同扫 → "materialAsset"；正常文件不含该键）
//   .novaprefab: 树形（root + children 递归），组件字段与场景同全集
//   build.novabuild: scenes[].path → "scene"
//   V1.0-4/5 扩展位（波 2 合波日由主会话按 §26.4 约定补扫描行）：
//     environment.postProcess.lutPath → "lut"、material.shaderGraphPath → "shaderGraph"
//
// 路径归一（§26.7-6，与 Packager 收集清单同口径）：相对项目根、正斜杠、无 ".."。
// 项目外的绝对路径（如系统字体 C:/Windows/Fonts/...）保持绝对形态，
// 存在性判定仍可命中（AssetStream 磁盘回退）。
//
// 重写纪律（§26.4）：nlohmann::json 读改写、键序 std::map 稳定、浮点最短往返无损；
// 只重写「已知引用字段」且归一后与源路径相等的字符串——不碰用户脚本任意字符串字段；
// 只有发生改写的文件才落盘；重写不 bump version。

#include <NovaEngine/Assets/AssetReferenceScanner.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <unordered_set>

namespace NovaEngine {

namespace {

namespace fs = std::filesystem;

constexpr int kMaxPrefabDepth = 8;   // 预制体嵌套深度上限（§22 既有纪律）

// ---------- 路径归一 ----------

// 项目相对 + 正斜杠 + 无 ".."（§26.7-6）。
// 绝对路径在项目根内 → 相对化；项目外（或跨盘）→ 保持 lexically_normal 绝对形态。
std::string normalizeProjectPath(const std::string& raw) {
    std::filesystem::path p(raw);
    if (p.is_absolute()) {
        const std::filesystem::path root = AssetDatabase::projectRoot();
        if (!root.empty()) {
            std::error_code ec;
            const std::filesystem::path rel = std::filesystem::relative(p, root, ec);
            if (!ec && !rel.empty() && *rel.begin() != "..")
                return rel.generic_string();
        }
        return p.lexically_normal().generic_string();
    }
    return p.lexically_normal().generic_string();
}

// 引用读取锚点：绝对路径原样；相对路径锚项目根（与 AssetDatabase::resolveAssetAbs 同语义的简化版）
std::filesystem::path toAbs(const std::filesystem::path& p) {
    return p.is_absolute() ? p : AssetDatabase::projectRoot() / p;
}

// 绝对路径 → 项目相对显示形态（失败回退绝对形态）
std::string toProjectRel(const std::filesystem::path& abs) {
    const std::filesystem::path root = AssetDatabase::projectRoot();
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(abs, root, ec);
    if (ec || rel.empty() || *rel.begin() == "..")
        return abs.lexically_normal().generic_string();
    return rel.generic_string();
}

// ---------- 扫描侧 ----------

// 追加一条引用（空路径与 builtin: 网格不是资产引用）
void addRef(std::vector<AssetReference>& out, const std::string& fromFile,
            const std::string& rawPath, const char* kind) {
    if (rawPath.empty())
        return;
    if (rawPath.starts_with("builtin:"))
        return;   // 内置网格（球/立方体等）非资产文件
    AssetReference r;
    r.assetPath = normalizeProjectPath(rawPath);
    if (r.assetPath.empty())
        return;
    r.kind = kind;
    r.fromFile = fromFile;
    out.push_back(std::move(r));
}

void scanMaterialNode(const nlohmann::json& mat, const std::string& fromFile,
                      std::vector<AssetReference>& out);
void scanPrefabInternal(const std::string& prefabPath, std::vector<AssetReference>& out,
                        int depth, std::unordered_set<std::string>& visited);

// 单组件引用收集（场景平铺与预制体树共用；onPrefab 命中 PrefabInstance 时回调递归）
void scanComponentPaths(const nlohmann::json& comp, const std::string& fromFile,
                        std::vector<AssetReference>& out, int depth,
                        std::unordered_set<std::string>& visited) {
    if (!comp.is_object())
        return;
    const std::string type = comp.value("type", std::string());

    if (type == "MeshRenderer" || type == "SkeletalMeshRenderer") {
        addRef(out, fromFile, comp.value("mesh", std::string()), "mesh");
        if (comp.contains("material") && comp["material"].is_object())
            scanMaterialNode(comp["material"], fromFile, out);
    } else if (type == "LODGroup") {
        if (comp.contains("lods") && comp["lods"].is_array())
            for (const auto& j : comp["lods"])
                if (j.is_object())
                    addRef(out, fromFile, j.value("mesh", std::string()), "mesh");
    } else if (type == "MeshCollider") {
        addRef(out, fromFile, comp.value("meshPath", std::string()), "mesh");
    } else if (type == "ParticleSystem" || type == "SpriteRenderer") {
        addRef(out, fromFile, comp.value("texturePath", std::string()), "texture");
    } else if (type == "Image") {
        addRef(out, fromFile, comp.value("spritePath", std::string()), "texture");
    } else if (type == "Slider") {
        addRef(out, fromFile, comp.value("backgroundSpritePath", std::string()), "texture");
        addRef(out, fromFile, comp.value("fillSpritePath", std::string()), "texture");
        addRef(out, fromFile, comp.value("handleSpritePath", std::string()), "texture");
    } else if (type == "Terrain") {
        addRef(out, fromFile, comp.value("heightmapPath", std::string()), "terrainHeightmap");
        addRef(out, fromFile, comp.value("splatmapPath", std::string()), "texture");
        if (comp.contains("layers") && comp["layers"].is_array())
            for (const auto& j : comp["layers"])
                if (j.is_object()) {
                    addRef(out, fromFile, j.value("albedo", std::string()), "texture");
                    addRef(out, fromFile, j.value("normal", std::string()), "texture");
                }
    } else if (type == "AudioSource") {
        addRef(out, fromFile, comp.value("clipPath", std::string()), "audioClip");
    } else if (type == "PrefabInstance") {
        const std::string prefabPath = comp.value("prefabPath", std::string());
        addRef(out, fromFile, prefabPath, "prefab");
        if (!prefabPath.empty() && depth < kMaxPrefabDepth)
            scanPrefabInternal(prefabPath, out, depth + 1, visited);
    } else if (type == "Text") {
        addRef(out, fromFile, comp.value("fontPath", std::string()), "font");
    }
    // V1.0-4/5 扩展位：PostProcess.lutPath → "lut"、Material.shaderGraphPath → "shaderGraph"
    // 波 2 合波日由主会话按 §26.4 约定补扫描行（kind 常量已冻结于头文件）
}

void scanMaterialNode(const nlohmann::json& mat, const std::string& fromFile,
                      std::vector<AssetReference>& out) {
    if (!mat.is_object())
        return;
    addRef(out, fromFile, mat.value("albedoMap", std::string()), "texture");
    addRef(out, fromFile, mat.value("normalMap", std::string()), "texture");
    addRef(out, fromFile, mat.value("metallicMap", std::string()), "texture");
    addRef(out, fromFile, mat.value("roughnessMap", std::string()), "texture");
    addRef(out, fromFile, mat.value("emissionMap", std::string()), "texture");
    addRef(out, fromFile, mat.value("materialAsset", std::string()), "materialAsset");
}

// 预制体树节点：components + children 递归
void scanTreeNode(const nlohmann::json& node, const std::string& fromFile,
                  std::vector<AssetReference>& out, int depth,
                  std::unordered_set<std::string>& visited) {
    if (!node.is_object())
        return;
    if (node.contains("components") && node["components"].is_array())
        for (const auto& c : node["components"])
            scanComponentPaths(c, fromFile, out, depth, visited);
    if (node.contains("children") && node["children"].is_array())
        for (const auto& ch : node["children"])
            scanTreeNode(ch, fromFile, out, depth, visited);
}

// 递归扫描预制体内部引用（visited 防环 + 深度上限；文件缺失由引用方条目记录，此处静默停止）
void scanPrefabInternal(const std::string& prefabPath, std::vector<AssetReference>& out,
                        int depth, std::unordered_set<std::string>& visited) {
    if (depth > kMaxPrefabDepth) {
        Logger::warn("引用扫描：预制体嵌套过深（>{} 层），停止递归: {}", kMaxPrefabDepth,
                     prefabPath);
        return;
    }
    const std::filesystem::path abs = toAbs(std::filesystem::path(prefabPath));
    const std::string key = normalizeProjectPath(abs.string());
    if (!visited.insert(key).second)
        return;   // 已扫（防环/同场景多实例去重）

    std::string text;
    if (!AssetStream::readAllText(abs, text))
        return;   // 缺失/损坏：不再深入（引用方条目已能定位）
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        Logger::warn("引用扫描：预制体 JSON 解析失败 {}（{}）", prefabPath, e.what());
        return;
    }
    if (!root.is_object())
        return;

    const std::string fromFile = toProjectRel(abs);
    if (root.contains("root") && root["root"].is_object())
        scanTreeNode(root["root"], fromFile, out, depth, visited);
}

// ---------- 重写侧（moveAsset） ----------

// 已知引用字段改写：归一后与 from 相等的字符串 → 替换为 to；命中一次计一条引用
bool rewriteField(nlohmann::json& parent, const char* key, const std::string& from,
                  const std::string& to, int& count) {
    if (!parent.contains(key) || !parent[key].is_string())
        return false;
    const std::string& v = parent[key].get_ref<const std::string&>();
    if (normalizeProjectPath(v) != from)
        return false;
    parent[key] = to;
    ++count;
    return true;
}

void rewriteMaterialNode(nlohmann::json& mat, const std::string& from, const std::string& to,
                         int& count) {
    if (!mat.is_object())
        return;
    rewriteField(mat, "albedoMap", from, to, count);
    rewriteField(mat, "normalMap", from, to, count);
    rewriteField(mat, "metallicMap", from, to, count);
    rewriteField(mat, "roughnessMap", from, to, count);
    rewriteField(mat, "emissionMap", from, to, count);
    rewriteField(mat, "materialAsset", from, to, count);
}

// 单组件引用改写（与扫描侧同字段全集；只改已知引用键）
void rewriteComponentPaths(nlohmann::json& comp, const std::string& from, const std::string& to,
                           int& count) {
    if (!comp.is_object())
        return;
    const std::string type = comp.value("type", std::string());

    if (type == "MeshRenderer" || type == "SkeletalMeshRenderer") {
        rewriteField(comp, "mesh", from, to, count);
        if (comp.contains("material") && comp["material"].is_object())
            rewriteMaterialNode(comp["material"], from, to, count);
    } else if (type == "LODGroup") {
        if (comp.contains("lods") && comp["lods"].is_array())
            for (auto& j : comp["lods"])
                if (j.is_object())
                    rewriteField(j, "mesh", from, to, count);
    } else if (type == "MeshCollider") {
        rewriteField(comp, "meshPath", from, to, count);
    } else if (type == "ParticleSystem" || type == "SpriteRenderer") {
        rewriteField(comp, "texturePath", from, to, count);
    } else if (type == "Image") {
        rewriteField(comp, "spritePath", from, to, count);
    } else if (type == "Slider") {
        rewriteField(comp, "backgroundSpritePath", from, to, count);
        rewriteField(comp, "fillSpritePath", from, to, count);
        rewriteField(comp, "handleSpritePath", from, to, count);
    } else if (type == "Terrain") {
        rewriteField(comp, "heightmapPath", from, to, count);
        rewriteField(comp, "splatmapPath", from, to, count);
        if (comp.contains("layers") && comp["layers"].is_array())
            for (auto& j : comp["layers"])
                if (j.is_object()) {
                    rewriteField(j, "albedo", from, to, count);
                    rewriteField(j, "normal", from, to, count);
                }
    } else if (type == "AudioSource") {
        rewriteField(comp, "clipPath", from, to, count);
    } else if (type == "PrefabInstance") {
        rewriteField(comp, "prefabPath", from, to, count);
    } else if (type == "Text") {
        rewriteField(comp, "fontPath", from, to, count);
    }
}

void rewriteTreeNode(nlohmann::json& node, const std::string& from, const std::string& to,
                     int& count) {
    if (!node.is_object())
        return;
    if (node.contains("components") && node["components"].is_array())
        for (auto& c : node["components"])
            rewriteComponentPaths(c, from, to, count);
    if (node.contains("children") && node["children"].is_array())
        for (auto& ch : node["children"])
            rewriteTreeNode(ch, from, to, count);
}

enum class JsonFileKind { Scene, Prefab, Material };

// 按文件形态遍历改写；返回本文件被改写的引用数
int rewriteJsonFile(nlohmann::json& root, JsonFileKind kind, const std::string& from,
                    const std::string& to) {
    if (!root.is_object())
        return 0;
    int count = 0;

    if (kind == JsonFileKind::Scene) {
        if (root.contains("environment") && root["environment"].is_object())
            rewriteField(root["environment"], "skyCubemapPath", from, to, count);
        if (root.contains("objects") && root["objects"].is_array())
            for (auto& obj : root["objects"])
                if (obj.is_object() && obj.contains("components") &&
                    obj["components"].is_array())
                    for (auto& c : obj["components"])
                        rewriteComponentPaths(c, from, to, count);
    } else if (kind == JsonFileKind::Prefab) {
        if (root.contains("root") && root["root"].is_object())
            rewriteTreeNode(root["root"], from, to, count);
    } else {   // Material
        if (root.contains("material") && root["material"].is_object())
            rewriteMaterialNode(root["material"], from, to, count);
    }
    return count;
}

// 读改写落盘（ofstream path 重载，中文路径安全；dump(4) 键序 std::map 稳定，§26.4）
bool rewriteFileOnDisk(const std::filesystem::path& abs, JsonFileKind kind,
                       const std::string& from, const std::string& to, int& totalCount) {
    nlohmann::json root;
    {
        std::ifstream ifs(abs, std::ios::binary);
        if (!ifs)
            return false;
        try {
            ifs >> root;
        } catch (const std::exception& e) {
            Logger::warn("引用重写：JSON 解析失败，跳过 {}（{}）", abs.string(), e.what());
            return false;
        }
    }
    const int n = rewriteJsonFile(root, kind, from, to);
    if (n == 0)
        return true;   // 无命中：不落盘，文件逐字节不变
    std::ofstream ofs(abs, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        Logger::error("引用重写：写入失败 {}（引用未更新）", abs.string());
        return false;
    }
    ofs << root.dump(4) << '\n';
    if (!ofs.good()) {
        Logger::error("引用重写：写入出错 {}（引用未更新）", abs.string());
        return false;
    }
    totalCount += n;
    Logger::info("引用重写：{} 更新 {} 条引用", toProjectRel(abs), n);
    return true;
}

// build.novabuild 场景条目改写
void rewriteBuildFile(const std::string& from, const std::string& to, int& totalCount) {
    const std::filesystem::path abs =
        AssetDatabase::projectRoot() / "assets" / "build.novabuild";
    std::ifstream ifs(abs, std::ios::binary);
    if (!ifs)
        return;   // 清单不存在 → 无引用方
    nlohmann::json doc;
    try {
        ifs >> doc;
    } catch (const std::exception& e) {
        Logger::warn("引用重写：build.novabuild 解析失败，跳过（{}）", e.what());
        return;
    }
    int n = 0;
    if (doc.is_object() && doc.contains("scenes") && doc["scenes"].is_array())
        for (auto& s : doc["scenes"])
            if (s.is_object())
                rewriteField(s, "path", from, to, n);
    if (n == 0)
        return;
    std::ofstream ofs(abs, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        Logger::error("引用重写：build.novabuild 写入失败（场景条目未更新）");
        return;
    }
    ofs << doc.dump(2) << '\n';
    if (!ofs.good()) {
        Logger::error("引用重写：build.novabuild 写入出错（场景条目未更新）");
        return;
    }
    totalCount += n;
    Logger::info("引用重写：assets/build.novabuild 更新 {} 条场景条目", n);
}

// 全项目引用重写（与 scanProject 同枚举口径：场景 + .novamat + .novaprefab + build.novabuild）
void rewriteProjectReferences(const std::string& from, const std::string& to, int& totalCount) {
    const std::filesystem::path assetsDir = AssetDatabase::projectRoot() / "assets";
    std::error_code ec;
    if (!std::filesystem::is_directory(assetsDir, ec))
        return;
    for (std::filesystem::recursive_directory_iterator it(
             assetsDir, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) { ec.clear(); continue; }
        const std::string name = it->path().filename().string();
        JsonFileKind kind;
        if (name.ends_with(".nova.json"))
            kind = JsonFileKind::Scene;
        else if (name.ends_with(".novaprefab"))
            kind = JsonFileKind::Prefab;
        else if (name.ends_with(".novamat"))
            kind = JsonFileKind::Material;
        else
            continue;
        rewriteFileOnDisk(it->path(), kind, from, to, totalCount);
    }
    rewriteBuildFile(from, to, totalCount);
}

}  // namespace

std::vector<AssetReference> AssetReferenceScanner::scanScene(
    const std::filesystem::path& scenePath) {
    std::vector<AssetReference> out;
    if (scenePath.empty())
        return out;
    const std::filesystem::path abs = toAbs(scenePath);
    std::string text;
    if (!AssetStream::readAllText(abs, text)) {
        Logger::error("引用扫描：场景读取失败 {}", scenePath.string());
        return out;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        Logger::error("引用扫描：场景 JSON 解析失败 {}（{}）", scenePath.string(), e.what());
        return out;
    }
    if (!root.is_object())
        return out;

    const std::string fromFile = toProjectRel(abs);
    if (root.contains("environment") && root["environment"].is_object())
        addRef(out, fromFile, root["environment"].value("skyCubemapPath", std::string()),
               "cubemap");

    std::unordered_set<std::string> visited;
    if (root.contains("objects") && root["objects"].is_array())
        for (const auto& obj : root["objects"]) {
            if (!obj.is_object() || !obj.contains("components") ||
                !obj["components"].is_array())
                continue;
            for (const auto& c : obj["components"])
                scanComponentPaths(c, fromFile, out, /*depth=*/0, visited);
        }
    return out;
}

std::vector<AssetReference> AssetReferenceScanner::scanMaterial(
    const std::filesystem::path& matPath) {
    std::vector<AssetReference> out;
    if (matPath.empty())
        return out;
    const std::filesystem::path abs = toAbs(matPath);
    std::string text;
    if (!AssetStream::readAllText(abs, text)) {
        Logger::error("引用扫描：材质资产读取失败 {}", matPath.string());
        return out;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        Logger::error("引用扫描：材质资产 JSON 解析失败 {}（{}）", matPath.string(), e.what());
        return out;
    }
    if (!root.is_object())
        return out;
    if (root.contains("material") && root["material"].is_object())
        scanMaterialNode(root["material"], toProjectRel(abs), out);
    return out;
}

std::vector<AssetReference> AssetReferenceScanner::scanPrefab(
    const std::filesystem::path& prefabPath) {
    std::vector<AssetReference> out;
    if (prefabPath.empty())
        return out;
    std::unordered_set<std::string> visited;
    scanPrefabInternal(prefabPath.string(), out, /*depth=*/0, visited);
    return out;
}

std::vector<AssetReference> AssetReferenceScanner::scanProject() {
    std::vector<AssetReference> out;
    const std::filesystem::path assetsDir = AssetDatabase::projectRoot() / "assets";
    std::error_code ec;
    if (!std::filesystem::is_directory(assetsDir, ec)) {
        Logger::warn("引用扫描：assets/ 目录不存在，扫描为空: {}", assetsDir.string());
        return out;
    }
    for (std::filesystem::recursive_directory_iterator it(
             assetsDir, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) { ec.clear(); continue; }
        const std::string name = it->path().filename().string();
        if (name.ends_with(".nova.json")) {
            std::vector<AssetReference> refs = scanScene(it->path());
            out.insert(out.end(), std::make_move_iterator(refs.begin()),
                       std::make_move_iterator(refs.end()));
        } else if (name.ends_with(".novamat")) {
            std::vector<AssetReference> refs = scanMaterial(it->path());
            out.insert(out.end(), std::make_move_iterator(refs.begin()),
                       std::make_move_iterator(refs.end()));
        } else if (name.ends_with(".novaprefab")) {
            std::vector<AssetReference> refs = scanPrefab(it->path());
            out.insert(out.end(), std::make_move_iterator(refs.begin()),
                       std::make_move_iterator(refs.end()));
        }
    }

    // build.novabuild 场景条目 → kind "scene"
    const std::filesystem::path buildAbs = assetsDir / "build.novabuild";
    std::string buildText;
    if (AssetStream::readAllText(buildAbs, buildText)) {
        try {
            const nlohmann::json doc = nlohmann::json::parse(buildText);
            if (doc.is_object() && doc.contains("scenes") && doc["scenes"].is_array()) {
                const std::string fromFile = "assets/build.novabuild";
                for (const auto& s : doc["scenes"])
                    if (s.is_object())
                        addRef(out, fromFile, s.value("path", std::string()), "scene");
            }
        } catch (const std::exception& e) {
            Logger::warn("引用扫描：build.novabuild 解析失败（{}）", e.what());
        }
    }

    // 去重：同一 (kind, assetPath, fromFile) 三元组只保留一条——
    // 场景内嵌递归（PrefabInstance → 预制体内部引用）与文件直扫（.novaprefab 自身）
    // 会重复命中；缺失列表粒度（kind/缺失路径/引用方）三元组即一行
    std::vector<AssetReference> unique;
    unique.reserve(out.size());
    std::unordered_set<std::string> seen;
    seen.reserve(out.size());
    for (AssetReference& r : out) {
        const std::string key = r.kind + '\n' + r.assetPath + '\n' + r.fromFile;
        if (seen.insert(key).second)
            unique.push_back(std::move(r));
    }
    return unique;
}

int AssetReferenceScanner::moveAsset(const std::filesystem::path& from,
                                     const std::filesystem::path& to) {
    const std::string fromNorm = normalizeProjectPath(from.string());
    const std::string toNorm = normalizeProjectPath(to.string());
    if (fromNorm.empty() || toNorm.empty()) {
        Logger::error("移动资产失败：路径为空");
        return -1;
    }
    const std::filesystem::path fromP(fromNorm), toP(toNorm);
    if (fromP.is_absolute() || toP.is_absolute() || fromNorm.starts_with("..") ||
        toNorm.starts_with("..")) {
        Logger::error("移动资产失败：路径必须在项目内（{} → {}）", fromNorm, toNorm);
        return -1;
    }
    if (fromNorm == toNorm) {
        Logger::error("移动资产失败：源与目标相同 {}", fromNorm);
        return -1;
    }
    const std::filesystem::path root = AssetDatabase::projectRoot();
    if (root.empty()) {
        Logger::error("移动资产失败：项目根未设置");
        return -1;
    }
    const std::filesystem::path absFrom = root / fromP;
    const std::filesystem::path absTo = root / toP;

    std::error_code ec;
    const bool fromIsDir = std::filesystem::is_directory(absFrom, ec);
    ec.clear();
    if (!fromIsDir && !AssetStream::exists(absFrom)) {
        Logger::error("移动资产失败：源不存在 {}", fromNorm);
        return -1;
    }
    if (std::filesystem::exists(absTo, ec)) {
        ec.clear();
        Logger::error("移动资产失败：目标已存在 {}", toNorm);
        return -1;
    }
    if (!std::filesystem::exists(absTo.parent_path(), ec)) {
        ec.clear();
        if (!std::filesystem::create_directories(absTo.parent_path(), ec)) {
            Logger::error("移动资产失败：无法创建目标目录 {}（{}）",
                          absTo.parent_path().string(), ec.message());
            return -1;
        }
    }

    // 磁盘移动（文件或目录——天空盒六面目录整体移动）
    std::filesystem::rename(absFrom, absTo, ec);
    if (ec) {
        Logger::error("移动资产失败：磁盘移动失败 {} → {}（{}）", fromNorm, toNorm,
                      ec.message());
        return -1;
    }
    // 贴图 .novameta 伴随文件同移（保持导入设置；无伴随文件时静默跳过）
    if (!fromIsDir) {
        const std::filesystem::path metaFrom = absFrom.string() + ".novameta";
        if (AssetStream::exists(metaFrom)) {
            std::error_code mec;
            std::filesystem::rename(metaFrom, absTo.string() + ".novameta", mec);
            if (mec)
                Logger::warn("移动资产：.novameta 伴随文件移动失败（{}），引用重写继续",
                             mec.message());
        }
    }

    // 全项目引用重写（场景 JSON/.novamat/.novaprefab/build.novabuild）
    int rewritten = 0;
    rewriteProjectReferences(fromNorm, toNorm, rewritten);
    Logger::info("移动资产完成：{} → {}（重写 {} 条引用）", fromNorm, toNorm, rewritten);
    return rewritten;
}

}  // namespace NovaEngine
