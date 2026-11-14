#include <NovaEngine/Scene/DirectionalLight.h>
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
}

void DirectionalLight::onDeserialize(const nlohmann::json& in) {
    color = jsonToVec3(in.value("color", nlohmann::json()), glm::vec3(1.0f));
    intensity = in.value("intensity", 1.0f);
}

}  // namespace NovaEngine
