#include "NeonPlayerWatcher.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Particles/ParticleSystem.h>
#include <NovaEngine/Scene/CharacterController.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaGame {

NeonPlayerWatcher::NeonPlayerWatcher(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonPlayerWatcher::onStart() {
    m_cc = gameObject()->getComponent<NovaEngine::CharacterController>();
    m_wasGrounded = m_cc ? m_cc->isGrounded() : false;
    // ===== V1.1 检查点：缓存全部平台（名称清单 = §28.3.6 平台表）=====
    if (NovaEngine::Scene* scene = gameObject()->scene()) {
        static const char* kPadNames[] = {"StartPad", "PadA2", "StepA1", "StepA2", "StepA3",
                                          "PadA4", "PadB1", "PadB2", "PadB3", "PadB4",
                                          "PadC1", "PadC2", "GatePad"};
        for (const char* n : kPadNames) {
            const auto found = scene->findGameObjectsByName(n);
            if (!found.empty())
                m_pads.push_back(found[0]);
        }
    }
    // ===== V1.1 Dash 拖尾粒子（Player 子对象）=====
    for (const auto& childPtr : gameObject()->children())
        if (childPtr->name() == "DashTrail")
            m_dashTrail = childPtr.get();
    NovaEngine::Logger::info("NeonPlayerWatcher: 挂载到 \"{}\"，复活点 ({:.1f},{:.1f},{:.1f})，"
                             "检查点平台 {} 个，CC {}",
                             gameObject()->name(), m_respawnPos.x, m_respawnPos.y, m_respawnPos.z,
                             m_pads.size(), m_cc ? "已连接" : "未找到!");
}

void NeonPlayerWatcher::onUpdate(float dt) {
    if (!enabled)
        return;

    const glm::vec3 pos = gameObject()->transform()->worldPosition();

    // ===== 掉落复活（§28.3.6）：y < -10 → 复位检查点 + 清速度 + sfx_fall；计数保留 =====
    // 必须经 CharacterController::setPosition 物理传送——只改 Transform 会被下一固定步
    // writeBackCharacters 覆盖回坠落位置（无限坠落死机，用户实测 bug，波 3 修复）
    if (pos.y < m_respawnY) {
        if (m_cc) {
            m_cc->setPosition(m_respawnPos);
            m_cc->setVelocity(glm::vec3(0.0f));
        }
        gameObject()->transform()->setLocalPosition(m_respawnPos);   // 视觉立即同步（Player 为根节点，local==world）
        if (auto* mgr = NeonGameManager::instance())
            mgr->playSfx("assets/audio/sfx_fall.wav", 1.0f);
        NovaEngine::Logger::info("NeonPlayerWatcher: 玩家掉落 y={:.2f} < {:.1f}，已复活到检查点 ({:.1f},{:.1f},{:.1f})（计数保留）",
                                 pos.y, m_respawnY, m_respawnPos.x, m_respawnPos.y, m_respawnPos.z);
        m_wasGrounded = false;
        m_prevVy = 0.0f;
        m_prevHorizSpeed = 0.0f;
        return;
    }

    if (!m_cc)
        return;
    const glm::vec3 vel = m_cc->velocity();
    const bool grounded = m_cc->isGrounded();
    const float horizSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);

    // ===== V1.1 检查点：着地站在某平台上且该平台沿路线更靠前（-z）→ 更新复活点 =====
    if (grounded) {
        for (NovaEngine::GameObject* pad : m_pads) {
            const NovaEngine::Transform* t = pad->transform();
            const glm::vec3 ps = t->localScale();
            const glm::vec3 pp = t->worldPosition();
            if (std::abs(pos.x - pp.x) <= ps.x * 0.5f && std::abs(pos.z - pp.z) <= ps.z * 0.5f
                && pp.z < m_respawnPos.z) {
                m_respawnPos = glm::vec3(pp.x, pp.y + ps.y * 0.5f + 0.9f, pp.z);
                NovaEngine::Logger::info("NeonPlayerWatcher: 检查点更新 → {} ({:.1f},{:.1f},{:.1f})",
                                         pad->name(), m_respawnPos.x, m_respawnPos.y, m_respawnPos.z);
                break;
            }
        }
    }

    // ===== 跳跃音效（§28.3.6）：着地下降沿 + 垂直速度向上（volume 0.5）=====
    if (m_wasGrounded && !grounded && vel.y > 0.0f) {
        if (auto* mgr = NeonGameManager::instance())
            mgr->playSfx("assets/audio/sfx_jump.wav", 0.5f);
    }
    // ===== V1.1 二段跳音效：空中 vy 单帧跃升 >4（重力只会让 vy 下降，跃升=注入起跳）=====
    if (!grounded && vel.y - m_prevVy > 4.0f && vel.y > 2.0f) {
        if (auto* mgr = NeonGameManager::instance())
            mgr->playSfx("assets/audio/sfx_jump.wav", 0.4f);
    }

    // ===== V1.1 Dash 反馈：水平速度 >8 上升沿（冲刺 = 4×2.5=10 m/s）→ 音效 + 拖尾粒子 =====
    if (horizSpeed > 8.0f && m_prevHorizSpeed <= 8.0f) {
        if (auto* mgr = NeonGameManager::instance())
            mgr->playSfx("assets/audio/sfx_dash.wav", 0.8f);
        if (m_dashTrail) {
            if (auto* ps = m_dashTrail->getComponent<NovaEngine::ParticleSystem>())
                ps->play();
        }
    }

    m_wasGrounded = grounded;
    m_prevVy = vel.y;
    m_prevHorizSpeed = horizSpeed;
}

void NeonPlayerWatcher::onSerialize(nlohmann::json& out) const {
    out["respawnY"] = m_respawnY;
    out["respawnPos"] = { m_respawnPos.x, m_respawnPos.y, m_respawnPos.z };
}

void NeonPlayerWatcher::onDeserialize(const nlohmann::json& in) {
    if (in.contains("respawnY") && in["respawnY"].is_number())
        m_respawnY = in["respawnY"].get<float>();
    if (in.contains("respawnPos") && in["respawnPos"].is_array() && in["respawnPos"].size() >= 3) {
        const auto& a = in["respawnPos"];
        if (a[0].is_number() && a[1].is_number() && a[2].is_number())
            m_respawnPos = glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
    }
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonPlayerWatcher)
