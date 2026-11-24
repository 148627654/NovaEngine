#include "NeonCollectible.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/Particles/ParticleSystem.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <cmath>
#include <nlohmann/json.hpp>

namespace NovaGame {

NeonCollectible::NeonCollectible(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonCollectible::onStart() {
    m_basePos = gameObject()->transform()->localPosition();
    // 相位按对象 id 派生（无随机源依赖，确定性且各球不同步）
    m_phase = static_cast<float>(gameObject()->id() % 16u) * 0.785f;
    m_collected = false;
}

void NeonCollectible::onUpdate(float dt) {
    if (!enabled || m_collected)
        return;

    // ===== 上浮摆动 + 自转（§28.3.6：y = 基础 y + sin(totalTime×bobSpeed + 相位)×bobHeight）=====
    const float t = NovaEngine::Time::totalTime();
    glm::vec3 pos = m_basePos;
    pos.y = m_basePos.y + std::sin(t * m_bobSpeed + m_phase) * m_bobHeight;
    gameObject()->transform()->setLocalPosition(pos);
    gameObject()->transform()->rotate(glm::vec3(0.0f, m_spinSpeedDeg * dt, 0.0f));

    // ===== 与玩家 3D 距离轮询（§28.3.6：半径 1.8m；平方距离比较免开方）=====
    NovaEngine::Scene* scene = gameObject()->scene();
    if (!scene)
        return;
    const auto players = scene->findGameObjectsByName("Player");
    if (players.empty())
        return;
    const glm::vec3 delta = players[0]->transform()->worldPosition()
                            - gameObject()->transform()->worldPosition();
    if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z
        >= m_pickupRadius * m_pickupRadius)
        return;

    // ===== 命中（§28.3.6 拾取流程）：Burst 粒子 → sfx → 计数 → 隐藏 =====
    for (const auto& childPtr : gameObject()->children()) {
        NovaEngine::GameObject* child = childPtr.get();
        if (child->name() == "Burst") {
            if (auto* ps = child->getComponent<NovaEngine::ParticleSystem>())
                ps->play();
        }
    }
    if (auto* mgr = NeonGameManager::instance()) {
        mgr->playSfx("assets/audio/sfx_pickup.wav", 1.0f);
        mgr->notifyCollected();
    }
    NovaEngine::Logger::info("NeonCollectible: 能量球被拾取 @ ({:.2f},{:.2f},{:.2f})",
                             pos.x, pos.y, pos.z);
    m_collected = true;
    gameObject()->setActiveSelf(false);
}

void NeonCollectible::onSerialize(nlohmann::json& out) const {
    out["pickupRadius"] = m_pickupRadius;
    out["bobHeight"] = m_bobHeight;
    out["bobSpeed"] = m_bobSpeed;
    out["spinSpeedDeg"] = m_spinSpeedDeg;
}

void NeonCollectible::onDeserialize(const nlohmann::json& in) {
    if (in.contains("pickupRadius") && in["pickupRadius"].is_number())
        m_pickupRadius = in["pickupRadius"].get<float>();
    if (in.contains("bobHeight") && in["bobHeight"].is_number())
        m_bobHeight = in["bobHeight"].get<float>();
    if (in.contains("bobSpeed") && in["bobSpeed"].is_number())
        m_bobSpeed = in["bobSpeed"].get<float>();
    if (in.contains("spinSpeedDeg") && in["spinSpeedDeg"].is_number())
        m_spinSpeedDeg = in["spinSpeedDeg"].get<float>();
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonCollectible)
