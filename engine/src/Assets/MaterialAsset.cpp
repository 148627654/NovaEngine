// MaterialAsset.cpp —— .novamat 材质资产读写（V0.3-6 材质库）
//
// JSON 结构（与场景 material 段同构，键名由 SerializationInternals 约定共享）：
//   { "version": 1, "name": "<文件名 stem>", "material": { ... } }
// material 段读写复用 SerializationInternals::writeMaterialJson/readMaterialJson
//（与场景/预制体同一套键名与默认值；贴图解析同一 sRGB 约定：
//  albedo 按 sRGB，normal/metallic/roughness/emission 按线性）。
// 防递归：资产材质自身不引用资产——读后清 materialAssetPath，写前清副本双轨字段
//（writeMaterialJson 对非空路径只写 "materialAsset" 键，该键不得出现在 .novamat 内）。
// 读收敛 AssetStream（pak 挂载时自动走内存）；写落盘 ofstream path 重载（中文路径安全，裁定 1）。

#include <NovaEngine/Assets/MaterialAsset.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include "../Serialization/SerializationInternals.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

namespace NovaEngine {

namespace {

// 与 AssetDatabase::resolveAssetAbs 同语义的读取锚点解析（该函数为 AssetDatabase.cpp 文件内
// static，此处按同一规则复制）：
//   pak 已挂载 → 恒用项目根锚点（pak 键按项目根降维）；
//   磁盘上项目根锚点存在 → 用之；
//   否则 cwd 锚点存在 → 用之（Player 裸盘模式资产位于 cwd）；
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

}  // namespace

std::shared_ptr<MaterialAsset> MaterialAsset::load(const std::filesystem::path& relPath) {
    if (relPath.empty()) {
        Logger::error("MaterialAsset: 加载失败（路径为空）");
        return nullptr;
    }

    const std::filesystem::path abs = resolveAssetAbs(relPath);
    std::string content;
    if (!AssetStream::readAllText(abs, content)) {
        Logger::error("MaterialAsset: 读取失败 {}", relPath.string());
        return nullptr;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(content);
    } catch (const std::exception& e) {
        Logger::error("MaterialAsset: JSON 解析失败 {}（{}）", relPath.string(), e.what());
        return nullptr;
    }
    if (!root.is_object()) {
        Logger::error("MaterialAsset: JSON 根节点不是对象 {}", relPath.string());
        return nullptr;
    }
    int version = 0;
    try {
        version = root.value("version", 0);
    } catch (const std::exception&) {
        // version 类型畸形 → 按 0 处理，下方 warn
    }
    if (version != 1)
        Logger::warn("MaterialAsset: 版本 {}（期望 1），仍尝试加载 {}", version, relPath.string());

    // 私有构造函数：只能经静态工厂创建（make_shared 不可见）
    auto asset = std::shared_ptr<MaterialAsset>(new MaterialAsset());
    asset->m_path = relPath.lexically_normal();
    const nlohmann::json kEmptyObject = nlohmann::json::object();
    const nlohmann::json& matNode =
        (root.contains("material") && root["material"].is_object()) ? root["material"] : kEmptyObject;
    // 畸形字段不崩溃：readMaterialJson 内部逐字段兜底，这里再兜一层（保留默认值）
    try {
        SerializationInternals::readMaterialJson(matNode, asset->m_material);
    } catch (const std::exception& e) {
        Logger::warn("MaterialAsset: material 段解析异常 {}（{}），保留默认值",
                     relPath.string(), e.what());
    }
    // 防递归：资产材质自身不引用资产（readMaterialJson 可能读入 "materialAsset" 键）
    asset->m_material.materialAssetPath.clear();
    asset->m_material.materialAsset.reset();
    return asset;
}

bool MaterialAsset::save(const std::filesystem::path& relPath, const Material& m) {
    if (relPath.empty()) {
        Logger::error("MaterialAsset: 保存失败（路径为空）");
        return false;
    }

    nlohmann::json root;
    root["version"] = 1;
    // name 取自文件名 stem（冻结头无 name 入参；创建流程按输入名命名文件，两者一致）
    root["name"] = relPath.stem().string();
    nlohmann::json mat;
    // 防递归：副本清双轨字段（writeMaterialJson 对非空 materialAssetPath 只写 "materialAsset"）
    Material clean = m;
    clean.materialAssetPath.clear();
    clean.materialAsset.reset();
    SerializationInternals::writeMaterialJson(mat, clean);
    root["material"] = std::move(mat);

    // 写盘锚点 = 项目根（写路径不回退 cwd：Player 不写资产）
    const std::filesystem::path abs = AssetDatabase::projectRoot() / relPath;
    std::error_code ec;
    std::filesystem::create_directories(abs.parent_path(), ec);   // 父目录可能不存在
    std::ofstream out(abs, std::ios::binary | std::ios::trunc);   // path 重载：中文路径安全
    if (!out) {
        Logger::error("MaterialAsset: 写入失败 {}", abs.string());
        return false;
    }
    out << root.dump(4) << '\n';
    if (!out.good()) {
        Logger::error("MaterialAsset: 写入出错 {}", abs.string());
        return false;
    }
    return true;
}

const Material& MaterialAsset::material() const {
    return m_material;
}

const std::filesystem::path& MaterialAsset::path() const {
    return m_path;
}

}  // namespace NovaEngine
