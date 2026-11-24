#include "NeonGate.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Particles/ParticleSystem.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <cmath>
#include <nlohmann/json.hpp>

namespace NovaGame {

NeonGate::NeonGate(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonGate::onStart() {
    NovaEngine::Scene* scene = gameObject()->scene();
    if (!scene)
        return;
    // 按名找门三对象 + 粒子对象（约定名见头文件注释；缺项只警告不崩溃）
    auto findOne = [scene](const std::string& name) -> NovaEngine::GameObject* {
        const auto found = scene->findGameObjectsByName(name);
        return found.empty() ? nullptr : found[0];
    };
    m_pillarL = findOne("GatePillarL");
    m_pillarR = findOne("GatePillarR");
    m_beam = findOne("GateBeam");
    m_aura = findOne("GateAura");
    if (!m_pillarL || !m_pillarR || !m_beam)
        NovaEngine::Logger::warn("NeonGate: 门对象未找齐（GatePillarL/GatePillarR/GateBeam），激活时缺失项跳过");
}

void NeonGate::onUpdate(float /*dt*/) {
    if (!enabled)
        return;
    NeonGameManager* mgr = NeonGameManager::instance();
    if (!mgr)
        return;

    // ===== 激活上升沿（§28.3.6）：计数满 → 材质换图 + 粒子 + 音效 =====
    if (!m_activated && mgr->gateActive()) {
        m_activated = true;
        const std::string graph = "assets/shadergraphs/neon_magenta.novashadergraph";
        // 三对象 MeshRenderer::material.shaderGraphPath 换 neon_magenta（兜底 emission 同 §28.3.6；
        // SceneRenderer 按 shaderGraphPath 分组重建，无需手动刷新）
        const auto apply = [&graph](NovaEngine::GameObject* go) {
            if (!go)
                return;
            if (auto* mr = go->getComponent<NovaEngine::MeshRenderer>()) {
                mr->material.shaderGraphPath = graph;
                mr->material.emissionColor = glm::vec3(0.8f, 0.1f, 1.2f);
                mr->material.emissionIntensity = 1.5f;
            }
        };
        apply(m_pillarL);
        apply(m_pillarR);
        apply(m_beam);
        if (m_aura) {
            if (auto* ps = m_aura->getComponent<NovaEngine::ParticleSystem>())
                ps->play();
        }
        mgr->playSfx("assets/audio/sfx_gate_open.wav", 1.0f);
        NovaEngine::Logger::info("NeonGate: 传送门已激活（材质切换 neon_magenta + GateAura 粒子 + sfx）");
    }

    // ===== 胜利区判定（激活后；§28.3.6）=====
    if (!m_activated || mgr->won())
        return;
    NovaEngine::Scene* scene = gameObject()->scene();
    if (!scene)
        return;
    const auto players = scene->findGameObjectsByName("Player");
    if (players.empty())
        return;
    const glm::vec3 p = players[0]->transform()->worldPosition();
    if (std::abs(p.x) <= m_zoneHalfX && p.z >= m_zoneZMin && p.z <= m_zoneZMax
        && p.y >= m_zoneYMin && p.y <= m_zoneYMax) {
        NovaEngine::Logger::info("NeonGate: 玩家进入胜利区 ({:.2f},{:.2f},{:.2f}) → 触发结算",
                                 p.x, p.y, p.z);
        mgr->finishRun();
    }
}

void NeonGate::onSerialize(nlohmann::json& out) const {
    out["zoneHalfX"] = m_zoneHalfX;
    out["zoneZMin"] = m_zoneZMin;
    out["zoneZMax"] = m_zoneZMax;
    out["zoneYMin"] = m_zoneYMin;
    out["zoneYMax"] = m_zoneYMax;
}

void NeonGate::onDeserialize(const nlohmann::json& in) {
    if (in.contains("zoneHalfX") && in["zoneHalfX"].is_number())
        m_zoneHalfX = in["zoneHalfX"].get<float>();
    if (in.contains("zoneZMin") && in["zoneZMin"].is_number())
        m_zoneZMin = in["zoneZMin"].get<float>();
    if (in.contains("zoneZMax") && in["zoneZMax"].is_number())
        m_zoneZMax = in["zoneZMax"].get<float>();
    if (in.contains("zoneYMin") && in["zoneYMin"].is_number())
        m_zoneYMin = in["zoneYMin"].get<float>();
    if (in.contains("zoneYMax") && in["zoneYMax"].is_number())
        m_zoneYMax = in["zoneYMax"].get<float>();
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonGate)
