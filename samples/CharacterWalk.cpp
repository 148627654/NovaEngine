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
    const bool grounded = cc->isGrounded();
    if (grounded)
        m_jumpCount = 0;    // 着地：跳跃次数重置（二段跳）

    // ===== V1.1 二段跳：着地跳（1）+ 空中跳（2）=====
    if (NovaEngine::InputMap::getActionDown("Jump") &&
        (grounded || m_jumpCount < 2)) {
        m_jumpLatched = true;
        m_jumpCount = grounded ? 1 : 2;
    }
    if (m_jumpLatched) {
        vel.y = m_jumpSpeed;
        // 解锁条件：物理已消费起跳（读回上跳速度）或超时兜底——二段跳发生在空中，
        // 不能用「离地」做解锁（会赶在物理消费前释放，重蹈吞跳竞态）
        m_jumpLatchTime += dt;
        if (cc->velocity().y > m_jumpSpeed * 0.5f || m_jumpLatchTime > 0.4f)
            m_jumpLatched = false;
    } else {
        m_jumpLatchTime = 0.0f;
    }

    // ===== V1.1 Dash 冲刺：Shift 边沿触发，0.25s 内水平速度 ×2.5，冷却 2s =====
    if (m_dashCooldown > 0.0f)
        m_dashCooldown -= dt;
    if (m_dashTime > 0.0f)
        m_dashTime -= dt;
    if (NovaEngine::InputMap::getActionDown("Dash") && m_dashCooldown <= 0.0f) {
        m_dashTime = 0.25f;
        m_dashCooldown = 2.0f;
    }
    const float speedScale = m_dashTime > 0.0f ? 2.5f : 1.0f;

    vel.x = h * m_speed * speedScale;    // A/D → 世界 X 轴
    vel.z = -v * m_speed * speedScale;   // W/S → 世界 -Z（相机在 +Z 一侧取景，前进 = 走向楼梯）
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
