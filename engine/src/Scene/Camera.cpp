#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <nlohmann/json.hpp>

namespace NovaEngine {

namespace {

glm::vec4 jsonToVec4(const nlohmann::json& j, const glm::vec4& fallback) {
    if (!j.is_array() || j.size() < 4)
        return fallback;
    try {
        return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

Camera::Camera(GameObject* owner) : Component(owner) {}

void Camera::onSerialize(nlohmann::json& out) const {
    out["fov"] = fov;
    out["nearPlane"] = nearPlane;
    out["farPlane"] = farPlane;
    out["clearColor"] = nlohmann::json::array({clearColor.x, clearColor.y, clearColor.z, clearColor.w});
    out["isMain"] = isMain;
}

void Camera::onDeserialize(const nlohmann::json& in) {
    fov = in.value("fov", 60.0f);
    nearPlane = in.value("nearPlane", 0.1f);
    farPlane = in.value("farPlane", 1000.0f);
    clearColor = jsonToVec4(in.value("clearColor", nlohmann::json()),
                            glm::vec4(0.18f, 0.22f, 0.35f, 1.0f));
    isMain = in.value("isMain", false);
}

}  // namespace NovaEngine

// 组件静态注册（V0.3 波 0；注册名经 ComponentRegistry 裁剪为 "Camera"）
NE_REGISTER_COMPONENT(NovaEngine::Camera)
