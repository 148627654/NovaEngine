#include "SpinningCube.h"
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <nlohmann/json.hpp>

namespace NovaGame {

SpinningCube::SpinningCube(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void SpinningCube::onStart() {}

void SpinningCube::onUpdate(float dt) {
    if (!enabled) return;
    transform()->rotate(glm::vec3(0.0f, m_speed * dt, 0.0f));   // dt 秒 × 度/秒
}

void SpinningCube::onSerialize(nlohmann::json& out) const { out["speed"] = m_speed; }
void SpinningCube::onDeserialize(const nlohmann::json& in) { m_speed = in.value("speed", 30.0f); }

}  // namespace NovaGame

// 脚本静态注册（注册名经 ScriptRegistry 裁剪为 "SpinningCube"）
NE_REGISTER_SCRIPT(NovaGame::SpinningCube)
