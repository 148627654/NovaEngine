#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <nlohmann/json.hpp>

#include <string>

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
    // V0.5-10 2D 投影（§25.8 键名）：projectionMode 字符串 + orthoSize（正交半高，米）
    out["projectionMode"] =
        projectionMode == ProjectionMode::Orthographic ? "Orthographic" : "Perspective";
    out["orthoSize"] = orthoSize;
}

void Camera::onDeserialize(const nlohmann::json& in) {
    fov = in.value("fov", 60.0f);
    nearPlane = in.value("nearPlane", 0.1f);
    farPlane = in.value("farPlane", 1000.0f);
    clearColor = jsonToVec4(in.value("clearColor", nlohmann::json()),
                            glm::vec4(0.18f, 0.22f, 0.35f, 1.0f));
    isMain = in.value("isMain", false);
    // V0.5-10 2D 投影：键缺失（旧文件）→ 缺省 Perspective = 旧行为零变化；
    // 未知字符串回退 Perspective（畸形文件不崩溃）
    const std::string mode = in.value("projectionMode", std::string("Perspective"));
    projectionMode =
        mode == "Orthographic" ? ProjectionMode::Orthographic : ProjectionMode::Perspective;
    orthoSize = in.value("orthoSize", 5.0f);
}

}  // namespace NovaEngine

// 组件静态注册（V0.3 波 0；注册名经 ComponentRegistry 裁剪为 "Camera"）
NE_REGISTER_COMPONENT(NovaEngine::Camera)
