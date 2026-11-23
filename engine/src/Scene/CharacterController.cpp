// ===== V1.0-3 CharacterController（§27.2）：骨架日桩实现（2026-11-23）=====
// 桩已含：组件注册（内置组件 25→26，ComponentRegistry link-guard 同步）、
// 序列化（§27.2 键名，缺省兜底）。物理行为（isGrounded/move 的 Jolt CharacterVirtual
// 封装、重力、固定步进集成、与 Rigidbody 互斥 warn）归 cR 波 2 在本文件填充。
#include <NovaEngine/Scene/CharacterController.h>
#include <NovaEngine/Scene/ComponentRegistry.h>

namespace NovaEngine {

CharacterController::CharacterController(GameObject* owner) : Component(owner) {}

bool CharacterController::isGrounded() const { return false; }   // 桩：cR 接 CharacterVirtual
void CharacterController::move(const glm::vec3& /*displacement*/) {}
glm::vec3 CharacterController::velocity() const { return m_velocity; }
void CharacterController::setVelocity(const glm::vec3& v) { m_velocity = v; }
void CharacterController::onAwake() {}
void CharacterController::onUpdate(float /*dt*/) {}

void CharacterController::onSerialize(nlohmann::json& out) const {
    out["radius"] = radius;
    out["height"] = height;
    out["stepHeight"] = stepHeight;
    out["slopeLimitDeg"] = slopeLimitDeg;
    out["mass"] = mass;
    out["gravityScale"] = gravityScale;
}

void CharacterController::onDeserialize(const nlohmann::json& in) {
    radius = in.value("radius", 0.4f);
    height = in.value("height", 1.8f);
    stepHeight = in.value("stepHeight", 0.35f);
    slopeLimitDeg = in.value("slopeLimitDeg", 45.0f);
    mass = in.value("mass", 70.0f);
    gravityScale = in.value("gravityScale", 1.0f);
}

}  // namespace NovaEngine

// 组件静态注册（§27.0：内置组件 25→26；注册名经 ComponentRegistry 裁剪为 "CharacterController"）
NE_REGISTER_COMPONENT(NovaEngine::CharacterController)
