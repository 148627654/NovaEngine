#include "MoveWithInput.h"

#include <NovaEngine/Core/InputMap.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace NovaGame {

MoveWithInput::MoveWithInput(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void MoveWithInput::onUpdate(float dt) {
    if (!enabled) return;
    NovaEngine::Transform* t = gameObject()->transform();
    if (!t) return;

    const float h = NovaEngine::InputMap::getAxis("Horizontal");
    const float v = NovaEngine::InputMap::getAxis("Vertical");
    if (h == 0.0f && v == 0.0f) return;

    glm::vec3 p = t->localPosition();
    p.x += h * m_speed * dt;
    p.z += v * m_speed * dt;   // Vertical 正 = 前进（默认 W=+1）
    t->setLocalPosition(p);
}

void MoveWithInput::onSerialize(nlohmann::json& out) const {
    out["speed"] = m_speed;
}

void MoveWithInput::onDeserialize(const nlohmann::json& in) {
    if (in.contains("speed") && in["speed"].is_number())
        m_speed = in["speed"].get<float>();
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::MoveWithInput)
