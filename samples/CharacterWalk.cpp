#include "CharacterWalk.h"

#include <NovaEngine/Core/InputMap.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/CharacterController.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaGame {

CharacterWalk::CharacterWalk(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void CharacterWalk::onStart() {
    // 挂载诊断（验收/排障可见）：CC 未找到时后续 onUpdate 全部静默跳过——这里必须打日志
    const auto* cc = gameObject()->getComponent<NovaEngine::CharacterController>();
    NovaEngine::Logger::info("CharacterWalk: 已挂载到 \"{}\"（CharacterController {}）",
                             gameObject()->name(), cc ? "已连接" : "未找到!");
}

void CharacterWalk::onUpdate(float dt) {
    if (!enabled) return;
    NovaEngine::CharacterController* cc = gameObject()->getComponent<NovaEngine::CharacterController>();
    if (!cc) return;

    const float h = NovaEngine::InputMap::getAxis("Horizontal");
    const float v = NovaEngine::InputMap::getAxis("Vertical");

    // 输入诊断（限频 1 次/秒）：确认轴读取与物理速度设置链路
    if ((h != 0.0f || v != 0.0f) && (m_logAccum += dt) > 1.0f) {
        m_logAccum = 0.0f;
        const glm::vec3 p = gameObject()->transform()->worldPosition();
        NovaEngine::Logger::info(
            "CharacterWalk: 轴(h={:.1f},v={:.1f}) → setVelocity 位置=({:.2f},{:.2f},{:.2f}) "
            "着地={}",
            h, v, p.x, p.y, p.z, cc->isGrounded());
    }

    // 垂直速度保留物理积分值（重力/落地清空由 PhysicsWorld 处理）；
    // 只有按下跳跃且着地时才注入起跳初速。起跳锁存（波 3 #10 修复）：高帧率下
    // 边沿帧注入的跳跃会被下一渲染帧的 setVelocity 覆盖吞掉（vel.y 从物理读回
    // 仍是起跳前值）——移动中每帧覆盖，「跑+跳」必吞跳。锁存到离地为止：
    // 连续注入起跳速度，物理固定步消费后 isGrounded=false → 解锁交还物理积分。
    glm::vec3 vel = cc->velocity();
    vel.x = h * m_speed;    // A/D → 世界 X 轴
    vel.z = -v * m_speed;   // W/S → 世界 -Z（相机在 +Z 一侧取景，前进 = 走向楼梯）
    if (NovaEngine::InputMap::getActionDown("Jump") && cc->isGrounded())
        m_jumpLatched = true;
    if (m_jumpLatched) {
        vel.y = m_jumpSpeed;
        if (!cc->isGrounded())
            m_jumpLatched = false;   // 已离地：垂直速度交还物理（重力/落地清空）
    }
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
