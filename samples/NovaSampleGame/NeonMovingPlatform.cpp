#include "NeonMovingPlatform.h"

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <cmath>
#include <nlohmann/json.hpp>

namespace NovaGame {

NeonMovingPlatform::NeonMovingPlatform(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonMovingPlatform::onStart() {
    // 快照基准位置（场景 JSON 的 center 即基准；重载后新实例重新快照）
    m_base = gameObject()->transform()->localPosition();
    // Reviewer P2-4：非法 axis 不再静默——warn 一次并说明兜底行为
    if (m_axis < 0 || m_axis > 2)
        NovaEngine::Logger::warn("NeonMovingPlatform: \"{}\" axis={} 非法（应为 0=X/1=Y/2=Z），按 Z 轴运动兜底",
                                 gameObject()->name(), m_axis);
    NovaEngine::Logger::info(
        "NeonMovingPlatform: \"{}\" 基准 ({:.2f},{:.2f},{:.2f})，轴 {} 振幅 {:.2f}m 周期 {:.1f}s 相位 {:.0f}°",
        gameObject()->name(), m_base.x, m_base.y, m_base.z, m_axis, m_amplitude,
        m_periodSec, m_phaseDeg);
}

void NeonMovingPlatform::onUpdate(float /*dt*/) {
    if (!enabled)
        return;
    if (m_periodSec <= 0.0f)
        return;   // 非法周期：静止不动（防除零）

    // ===== §28.3.15.3 运动公式：位置 = 基准 + sin(totalTime×2π/周期 + 相位)×振幅×轴向 =====
    const float t = NovaEngine::Time::totalTime();
    const float phase = m_phaseDeg * (3.14159265f / 180.0f);
    const float offset = std::sin(6.2831853f * t / m_periodSec + phase) * m_amplitude;
    glm::vec3 pos = m_base;
    if (m_axis == 0)
        pos.x += offset;
    else if (m_axis == 1)
        pos.y += offset;
    else
        pos.z += offset;
    gameObject()->transform()->setLocalPosition(pos);
}

void NeonMovingPlatform::onSerialize(nlohmann::json& out) const {
    out["axis"] = m_axis;
    out["amplitude"] = m_amplitude;
    out["periodSec"] = m_periodSec;
    out["phaseDeg"] = m_phaseDeg;
}

void NeonMovingPlatform::onDeserialize(const nlohmann::json& in) {
    if (in.contains("axis") && in["axis"].is_number_integer())
        m_axis = in["axis"].get<int>();
    if (in.contains("amplitude") && in["amplitude"].is_number())
        m_amplitude = in["amplitude"].get<float>();
    if (in.contains("periodSec") && in["periodSec"].is_number())
        m_periodSec = in["periodSec"].get<float>();
    if (in.contains("phaseDeg") && in["phaseDeg"].is_number())
        m_phaseDeg = in["phaseDeg"].get<float>();
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonMovingPlatform)
