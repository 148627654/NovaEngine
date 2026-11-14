#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <nlohmann/json.hpp>

namespace NovaEngine {

namespace {

glm::vec3 jsonToVec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    try {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

MeshRenderer::MeshRenderer(GameObject* owner) : Component(owner) {}

void MeshRenderer::onSerialize(nlohmann::json& out) const {
    out["mesh"] = meshPath;
    nlohmann::json mat;
    mat["albedo"] = nlohmann::json::array({material.albedo.x, material.albedo.y, material.albedo.z});
    mat["roughness"] = material.roughness;
    mat["metallic"] = material.metallic;
    // JSON 键名保持 schema 的 "albedoMap" 不变，值取自 material.albedoMapPath（空串 = 纯色）
    mat["albedoMap"] = material.albedoMapPath;
    out["material"] = std::move(mat);
}

void MeshRenderer::onDeserialize(const nlohmann::json& in) {
    // mesh 路径经 AssetDatabase 解析（Coder C 的实现，此处只调用冻结头）
    meshPath = in.value("mesh", std::string());
    if (!meshPath.empty()) {
        mesh = AssetDatabase::instance().getMesh(std::filesystem::path(meshPath));
        if (!mesh)
            Logger::warn("MeshRenderer 加载网格失败：{}（保持空网格，不崩溃）", meshPath);
    } else {
        mesh = nullptr;
    }

    const auto& mat = in.value("material", nlohmann::json::object());
    material.albedo = jsonToVec3(mat.value("albedo", nlohmann::json()), glm::vec3(0.8f));
    material.roughness = mat.value("roughness", 0.6f);
    material.metallic = mat.value("metallic", 0.0f);

    // 先存路径（序列化往返用），再经 AssetDatabase::getTexture 解析到运行时纹理
    material.albedoMapPath = mat.value("albedoMap", std::string());
    if (!material.albedoMapPath.empty()) {
        material.albedoMap = AssetDatabase::instance().getTexture(std::filesystem::path(material.albedoMapPath));
        if (!material.albedoMap)
            Logger::warn("MeshRenderer 加载贴图失败：{}（回退纯色）", material.albedoMapPath);
    } else {
        material.albedoMap = nullptr;
    }
}

}  // namespace NovaEngine
