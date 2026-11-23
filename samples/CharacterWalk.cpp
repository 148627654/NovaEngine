#include "CharacterWalk.h"

#include <NovaEngine/Core/InputMap.h>
#include <NovaEngine/Scene/CharacterController.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaGame {

CharacterWalk::CharacterWalk(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void CharacterWalk::onUpdate(float /*dt*/) {
    if (!enabled) return;
    NovaEngine::CharacterController* cc = gameObject()->getComponent<NovaEngine::CharacterController>();
    if (!cc) return;

    const float h = NovaEngine::InputMap::getAxis("Horizontal");
    const float v = NovaEngine::InputMap::getAxis("Vertical");

    // 垂直速度保留物理积分值（重力/落地清空由 PhysicsWorld 处理）；
    // 只有按下跳跃且着地时才注入起跳初速（单帧注入，下一固定步消费）
    glm::vec3 vel = cc->velocity();
    vel.x = h * m_speed;    // A/D → 世界 X 轴
    vel.z = -v * m_speed;   // W/S → 世界 -Z（相机在 +Z 一侧取景，前进 = 走向楼梯）
    if (NovaEngine::InputMap::getActionDown("Jump") && cc->isGrounded())
        vel.y = m_jumpSpeed;
    cc->setVelocity(vel);   // 每帧覆盖（pending override，PhysicsWorld 固定步消费）
}

void CharacterWalk::onSerialize(nlohmann::json& out) const {
    out["speed"] = m_speed;
    out["jumpSpeed"] = m_jumpSpeed;
}

void CharacterWalk::onDeserialize(const nlohmann::json& in) {
    if (in.contains("speed") && in["speed"].is_number())
        m_speed = in["speed"].get<float>();
    if (in.contains("jumpSpeed") && in["jumpSpeed"].is_number())
        m_jumpSpeed = in["jumpSpeed"].get<float>();
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::CharacterWalk)
