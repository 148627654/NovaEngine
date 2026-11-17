#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// 验收脚本（V0.5-11 手柄三态验收）：InputMap 轴驱动宿主平移——
// 键鼠 A/D + W/S，或手柄左摇杆（内置默认绑定 Horizontal/Vertical 自动生效）
class MoveWithInput : public NovaEngine::Behaviour {
public:
    explicit MoveWithInput(NovaEngine::GameObject* owner);
    float m_speed = 5.0f;                                // 米/秒，Inspector 可调
    void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;   // 写 "speed"
    void onDeserialize(const nlohmann::json& in) override;  // 读 "speed"
};

}  // namespace NovaGame
