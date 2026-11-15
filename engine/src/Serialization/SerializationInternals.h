#pragma once
// 内部头（非公共 API，不放 include/NovaEngine）：material 段 JSON 序列化辅助。
// MeshRenderer / PrefabSerializer（波 4）/ MaterialAsset（波 1）共用；
// 从 MeshRenderer.cpp 原实现逐字节提取，JSON 键名与读默认值保持 V0.2-8 schema 一致：
// albedo / roughness / metallic / albedoMap / alpha / blendMode。
#include <filesystem>
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

// material 段 → JSON 节点（调用方负责把节点挂到 "material" 键，或用作 .novamat 根）
inline void writeMaterialJson(nlohmann::json& out, const Material& material) {
    out["albedo"] = nlohmann::json::array({material.albedo.x, material.albedo.y, material.albedo.z});
    out["roughness"] = material.roughness;
    out["metallic"] = material.metallic;
    // JSON 键名保持 schema 的 "albedoMap" 不变，值取自 material.albedoMapPath（空串 = 纯色）
    out["albedoMap"] = material.albedoMapPath;
    // V0.2-8：alpha + blendMode（字符串 "Opaque"/"AlphaBlend"）
    out["alpha"] = material.alpha;
    out["blendMode"] = material.blendMode == BlendMode::AlphaBlend ? "AlphaBlend" : "Opaque";
}

// material 段 JSON 节点 → Material（键名、默认值与 MeshRenderer 原实现逐字节一致）
inline void readMaterialJson(const nlohmann::json& in, Material& material) {
    material.albedo = jsonToVec3(in.value("albedo", nlohmann::json()), glm::vec3(0.8f));
    material.roughness = in.value("roughness", 0.6f);
    material.metallic = in.value("metallic", 0.0f);

    // 先存路径（序列化往返用），再经 AssetDatabase::getTexture 解析到运行时纹理
    material.albedoMapPath = in.value("albedoMap", std::string());
    if (!material.albedoMapPath.empty()) {
        material.albedoMap = AssetDatabase::instance().getTexture(std::filesystem::path(material.albedoMapPath));
        if (!material.albedoMap)
            Logger::warn("MeshRenderer 加载贴图失败：{}（回退纯色）", material.albedoMapPath);
    } else {
        material.albedoMap = nullptr;
    }

    // V0.2-8：旧文件（schema 1/2）无 alpha/blendMode 字段 → 默认值兜底（1.0 / Opaque）
    material.alpha = in.value("alpha", 1.0f);
    const std::string blendStr = in.value("blendMode", std::string("Opaque"));
    if (blendStr == "AlphaBlend") {
        material.blendMode = BlendMode::AlphaBlend;
    } else {
        if (blendStr != "Opaque")
            Logger::warn("MeshRenderer 反序列化：未知 blendMode \"{}\"，回退 Opaque", blendStr);
        material.blendMode = BlendMode::Opaque;
    }
}

}  // namespace SerializationInternals
}  // namespace NovaEngine
