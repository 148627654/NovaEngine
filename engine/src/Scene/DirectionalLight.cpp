#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
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

DirectionalLight::DirectionalLight(GameObject* owner) : Component(owner) {}

void DirectionalLight::onSerialize(nlohmann::json& out) const {
    out["color"] = nlohmann::json::array({color.x, color.y, color.z});
    out["intensity"] = intensity;
    // 阴影（V0.2-6）
    out["castShadows"] = castShadows;
    out["shadowBias"] = shadowBias;
    out["shadowRange"] = shadowRange;
    out["shadowMapSize"] = shadowMapSize;
}

void DirectionalLight::onDeserialize(const nlohmann::json& in) {
    color = jsonToVec3(in.value("color", nlohmann::json()), glm::vec3(1.0f));
    intensity = in.value("intensity", 1.0f);
    // 阴影字段（V0.2-6）：in.value(key, 默认值) 兜底，旧 JSON（version 1）开箱即用。
    // 字段类型畸形时抛出的异常由 SceneSerializer::deserialize 的统一 try/catch 接住。
    castShadows = in.value("castShadows", true);
    shadowBias = in.value("shadowBias", 0.0005f);
    shadowRange = in.value("shadowRange", 60.0f);
    shadowMapSize = in.value("shadowMapSize", 2048);
}

}  // namespace NovaEngine

// 组件静态注册（V0.3 波 0；注册名经 ComponentRegistry 裁剪为 "DirectionalLight"）
NE_REGISTER_COMPONENT(NovaEngine::DirectionalLight)
