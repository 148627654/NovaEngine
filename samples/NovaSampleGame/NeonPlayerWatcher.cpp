#include "NeonPlayerWatcher.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/CharacterController.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaGame {

NeonPlayerWatcher::NeonPlayerWatcher(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonPlayerWatcher::onStart() {
    m_cc = gameObject()->getComponent<NovaEngine::CharacterController>();
    m_wasGrounded = m_cc ? m_cc->isGrounded() : false;
    NovaEngine::Logger::info("NeonPlayerWatcher: 挂载到 \"{}\"，复活点 ({:.1f},{:.1f},{:.1f})，CC {}",
                             gameObject()->name(), m_respawnPos.x, m_respawnPos.y, m_respawnPos.z,
                             m_cc ? "已连接" : "未找到!");
}

void NeonPlayerWatcher::onUpdate(float /*dt*/) {
    if (!enabled)
        return;

    const glm::vec3 pos = gameObject()->transform()->worldPosition();

    // ===== 掉落复活（§28.3.6）：y < -10 → 复位 + 清速度 + sfx_fall；计数保留 =====
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
        NovaEngine::Logger::info("NeonPlayerWatcher: 玩家掉落 y={:.2f} < {:.1f}，已复活到出生点（计数保留）",
                                 pos.y, m_respawnY);
        m_wasGrounded = false;
        return;
    }

    // ===== 跳跃音效（§28.3.6）：isGrounded 下降沿 + 垂直速度向上（volume 0.5）=====
    if (m_cc) {
        const bool grounded = m_cc->isGrounded();
        if (m_wasGrounded && !grounded && m_cc->velocity().y > 0.0f) {
            if (auto* mgr = NeonGameManager::instance())
                mgr->playSfx("assets/audio/sfx_jump.wav", 0.5f);
        }
        m_wasGrounded = grounded;
    }
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
