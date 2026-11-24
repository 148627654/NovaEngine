#include "NeonCameraShake.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/Time.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <cmath>

namespace NovaGame {

NeonCameraShake::NeonCameraShake(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonCameraShake::onStart() {
    if (gameObject()->transform())
        m_basePos = gameObject()->transform()->localPosition();
    m_shakeTime = 0.0f;
    if (NeonGameManager* mgr = NeonGameManager::instance())
        m_lastCollected = mgr->collected();
}

void NeonCameraShake::onUpdate(float dt) {
    if (!enabled)
        return;
    // 收集数上升沿 → 触发抖动
    if (NeonGameManager* mgr = NeonGameManager::instance()) {
        const int c = mgr->collected();
        if (c > m_lastCollected)
            m_shakeTime = duration;
        m_lastCollected = c;
    }

    if (!gameObject()->transform())
        return;
    if (m_shakeTime > 0.0f) {
        m_shakeTime -= dt;
        const float decay = std::max(0.0f, m_shakeTime / std::max(duration, 1e-3f));
        const float t = NovaEngine::Time::totalTime();
        const glm::vec3 off(std::sin(t * 60.0f) * intensity * decay,
                            std::cos(t * 53.0f) * intensity * 0.7f * decay, 0.0f);
        gameObject()->transform()->setLocalPosition(m_basePos + off);
    } else if (m_shakeTime <= 0.0f) {
        // 抖动结束：精确复位基准位置
        gameObject()->transform()->setLocalPosition(m_basePos);
    }
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonCameraShake)
