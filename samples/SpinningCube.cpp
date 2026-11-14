#include "SpinningCube.h"
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <nlohmann/json.hpp>

namespace NovaGame {

SpinningCube::SpinningCube(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void SpinningCube::onStart() {
    m_logFrames = 0;
    m_totalDegrees = 0.0f;
    // Play 启动广播证据（配合前 3 帧的 onUpdate 日志验证「进入 Play 后脚本确实在跑」）
    NovaEngine::Logger::info("SpinningCube::onStart（{}，speed={}°/s）", gameObject()->name(), m_speed);
}

void SpinningCube::onUpdate(float dt) {
    if (!enabled) return;
    transform()->rotate(glm::vec3(0.0f, m_speed * dt, 0.0f));   // dt 秒 × 度/秒
    m_totalDegrees += m_speed * dt;

    // 仅进入 Play 后的前 3 帧打日志（证明 onUpdate 每帧广播且 dt 非零；避免长时间刷屏）
    if (m_logFrames < 3) {
        ++m_logFrames;
        NovaEngine::Logger::info("SpinningCube 旋转中: dt={:.4f}s，累计 {:.1f}°", dt, m_totalDegrees);
    }
}

void SpinningCube::onSerialize(nlohmann::json& out) const { out["speed"] = m_speed; }
void SpinningCube::onDeserialize(const nlohmann::json& in) { m_speed = in.value("speed", 30.0f); }

}  // namespace NovaGame

// 脚本静态注册（注册名经 ScriptRegistry 裁剪为 "SpinningCube"）
NE_REGISTER_SCRIPT(NovaGame::SpinningCube)
