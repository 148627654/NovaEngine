#include "OrbitCamera.h"
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <cmath>
#include <nlohmann/json.hpp>
#include <glm/gtc/constants.hpp>

namespace NovaGame {

OrbitCamera::OrbitCamera(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void OrbitCamera::onStart() {
    // 从当前世界位置反推起始角度，保证从相机摆放处无缝开始环绕
    const glm::vec3 p = transform()->worldPosition();
    m_angle = glm::degrees(std::atan2(p.z, p.x));
}

void OrbitCamera::onUpdate(float dt) {
    if (!enabled) return;
    m_angle += m_speed * dt;
    // 归一化到 [0, 360) 度
    if (m_angle >= 360.0f) m_angle -= 360.0f;
    if (m_angle < 0.0f) m_angle += 360.0f;

    // 绕 Y 轴环绕：position = (cos(a) * radius, height, sin(a) * radius)
    const float rad = glm::radians(m_angle);
    transform()->setLocalPosition(
        glm::vec3(std::cos(rad) * m_radius, m_height, std::sin(rad) * m_radius));
    transform()->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
}

void OrbitCamera::onSerialize(nlohmann::json& out) const {
    out["radius"] = m_radius;
    out["speed"] = m_speed;
    out["height"] = m_height;
}

void OrbitCamera::onDeserialize(const nlohmann::json& in) {
    m_radius = in.value("radius", 8.0f);
    m_speed = in.value("speed", 30.0f);
    m_height = in.value("height", 3.0f);
}

}  // namespace NovaGame

// 脚本静态注册（注册名经 ScriptRegistry 裁剪为 "OrbitCamera"）
NE_REGISTER_SCRIPT(NovaGame::OrbitCamera)
