#pragma once
#include <glm/vec3.hpp>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// V1.1 收集镜头震动：检测 NeonGameManager 收集数变化 → 短暂抖动相机局部位置。
// 挂载：Main Camera 子物体（与相机跟随 Player 的层级共存）。
class NeonCameraShake : public NovaEngine::Behaviour {
public:
    explicit NeonCameraShake(NovaEngine::GameObject* owner);

    float intensity = 0.08f;   // 抖动幅度（米，Inspector 可调）
    float duration = 0.25f;    // 抖动时长（秒）

    void onStart() override;
    void onUpdate(float dt) override;

private:
    glm::vec3 m_basePos{0.0f};   // 相机基准局部位置（onStart 快照）
    float m_shakeTime = 0.0f;    // 剩余抖动时间
    int m_lastCollected = 0;     // 上一帧收集数（上升沿检测）
};

}  // namespace NovaGame
