#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// 示例脚本：绕自身 Y 轴匀速旋转立方体（M5 验收用）
class SpinningCube : public NovaEngine::Behaviour {
public:
    explicit SpinningCube(NovaEngine::GameObject* owner);
    float m_speed = 30.0f;                                      // 度/秒，Inspector 可调
    void onStart() override;                                    // 记录初始欧拉角（可选）
    void onUpdate(float dt) override;                           // 绕自身 Y 轴旋转
    void onSerialize(nlohmann::json& out) const override;       // 写 "speed"
    void onDeserialize(const nlohmann::json& in) override;      // 读 "speed"
};

}  // namespace NovaGame
