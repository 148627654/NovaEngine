#pragma once
// 内部头（非公共 API，不放 include/NovaEngine）：material 段 JSON 序列化辅助。
// MeshRenderer / PrefabSerializer（波 4）/ MaterialAsset（波 1）共用；
// 基础键名与读默认值保持 V0.2-8 schema 一致：
// albedo / roughness / metallic / albedoMap / alpha / blendMode。
// V0.3-4（PBR）：+normalMap/metallicMap/roughnessMap/emissionMap/emissionColor/emissionIntensity。
// V0.3-6（材质库双轨）：+materialAsset；materialAssetPath 非空时只写资产路径、省略内联 material 段。
#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Material.h>

namespace NovaEngine {
namespace SerializationInternals {

// 字段类型不符（如 albedo 是字符串）时回退默认值，畸形文件不崩溃
inline glm::vec3 jsonToVec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    try {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

// 字符串字段读取：键缺失或类型不符（如数字）回退默认值
inline std::string jsonToString(const nlohmann::json& j, const char* key, const std::string& fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string())
        return fallback;
    return j[key].get<std::string>();
}

// 浮点字段读取：键缺失或类型不符回退默认值
inline float jsonToFloat(const nlohmann::json& j, const char* key, float fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number())
        return fallback;
    try {
        return j[key].get<float>();
    } catch (const std::exception&) {
        return fallback;
    }
}

// material → JSON 节点（调用方负责把节点挂到 "material" 键，或用作 .novamat 根）
inline void writeMaterialJson(nlohmann::json& out, const Material& material) {
    // V0.3-6 材质库双轨：与渲染端指针门一致——materialAsset 已解析 或 materialAssetPath 非空
    // 均视为整块引用资产 → 只写 "materialAsset" 键并省略内联 material 段（无 per-instance override）
    if (material.materialAsset || !material.materialAssetPath.empty()) {
        out["materialAsset"] = material.materialAssetPath;
        return;
    }

    out["albedo"] = nlohmann::json::array({material.albedo.x, material.albedo.y, material.albedo.z});
    out["roughness"] = material.roughness;
    out["metallic"] = material.metallic;
    // JSON 键名保持 schema 的 "albedoMap" 不变，值取自 material.albedoMapPath（空串 = 纯色）
    out["albedoMap"] = material.albedoMapPath;
    // V0.2-8：alpha + blendMode（字符串 "Opaque"/"AlphaBlend"）
    out["alpha"] = material.alpha;
    out["blendMode"] = material.blendMode == BlendMode::AlphaBlend ? "AlphaBlend" : "Opaque";
    // V0.3-4 PBR 新键：贴图一律写路径字符串（空串 = 无贴图）
    out["normalMap"] = material.normalMapPath;
    out["metallicMap"] = material.metallicMapPath;
    out["roughnessMap"] = material.roughnessMapPath;
    out["emissionMap"] = material.emissionMapPath;
    out["emissionColor"] = nlohmann::json::array(
        {material.emissionColor.x, material.emissionColor.y, material.emissionColor.z});
    out["emissionIntensity"] = material.emissionIntensity;
}

// material 段 JSON 节点 → Material（旧键默认值与 V0.2-8 一致；新键 V0.3-4/6 默认值兜底）
inline void readMaterialJson(const nlohmann::json& in, Material& material) {
    // 入口防御（P2-5）：material 段不是 JSON 对象（字符串/数组等畸形形态）时，
    // 全部字段填默认值并返回——「畸形文件不崩溃」不依赖调用方守卫，
    // MaterialAsset::load 等直接调用方同样受保护
    if (!in.is_object()) {
        material = Material{};
        return;
    }

    // V0.3-6 双轨：materialAsset 非空 = 以资产为准（渲染用 materialAsset->material()）。
    // 资产指针由调用方（MeshRenderer::onDeserialize）经 AssetDatabase 解析；
    // 内联字段仍读入，作为资产加载失败时的回退值（"非空 + 内联同时存在 → 资产为准"）
    material.materialAssetPath = jsonToString(in, "materialAsset", std::string());

    material.albedo = jsonToVec3(in.value("albedo", nlohmann::json()), glm::vec3(0.8f));
    material.roughness = jsonToFloat(in, "roughness", 0.6f);
    material.metallic = jsonToFloat(in, "metallic", 0.0f);

    // 贴图路径（序列化往返用）→ 运行时纹理（经 AssetDatabase 解析）：
    // albedo 按 sRGB 加载（V0.3-4 起，与 .novameta 声明共同决定内部格式）；
    // normal/metallic/roughness/emission 一律线性——法线必须线性，否则切线空间光照错误
    auto resolveTexture = [](const std::string& path, bool srgb) -> std::shared_ptr<Texture> {
        if (path.empty())
            return nullptr;
        auto tex = AssetDatabase::instance().getTexture(std::filesystem::path(path), srgb);
        if (!tex)
            Logger::warn("材质加载贴图失败：{}（回退默认）", path);
        return tex;
    };
    material.albedoMapPath = jsonToString(in, "albedoMap", std::string());
    material.albedoMap = resolveTexture(material.albedoMapPath, /*srgb=*/true);
    material.normalMapPath = jsonToString(in, "normalMap", std::string());
    material.normalMap = resolveTexture(material.normalMapPath, /*srgb=*/false);
    material.metallicMapPath = jsonToString(in, "metallicMap", std::string());
    material.metallicMap = resolveTexture(material.metallicMapPath, /*srgb=*/false);
    material.roughnessMapPath = jsonToString(in, "roughnessMap", std::string());
    material.roughnessMap = resolveTexture(material.roughnessMapPath, /*srgb=*/false);
    material.emissionMapPath = jsonToString(in, "emissionMap", std::string());
    material.emissionMap = resolveTexture(material.emissionMapPath, /*srgb=*/false);

    // V0.3-4：自发光（emissionColor 缺省 [0,0,0]，emissionIntensity 缺省 1.0）
    material.emissionColor = jsonToVec3(in.value("emissionColor", nlohmann::json()), glm::vec3(0.0f));
    material.emissionIntensity = jsonToFloat(in, "emissionIntensity", 1.0f);

    // V0.2-8：旧文件（schema 1/2）无 alpha/blendMode 字段 → 默认值兜底（1.0 / Opaque）
    material.alpha = jsonToFloat(in, "alpha", 1.0f);
    const std::string blendStr = jsonToString(in, "blendMode", std::string("Opaque"));
    if (blendStr == "AlphaBlend") {
        material.blendMode = BlendMode::AlphaBlend;
    } else {
        if (blendStr != "Opaque")
            Logger::warn("材质反序列化：未知 blendMode \"{}\"，回退 Opaque", blendStr);
        material.blendMode = BlendMode::Opaque;
    }
}

}  // namespace SerializationInternals
}  // namespace NovaEngine
