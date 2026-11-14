#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// 示例脚本：相机绕 Y 轴环绕原点并 lookAt 原点（Play 模式演示用）
class OrbitCamera : public NovaEngine::Behaviour {
public:
    explicit OrbitCamera(NovaEngine::GameObject* owner);

    float m_radius = 8.0f;                                      // 环绕半径（米），Inspector 可调
    float m_speed = 30.0f;                                      // 角速度（度/秒），Inspector 可调
    float m_height = 3.0f;                                      // 环绕高度（米），Inspector 可调

    void onStart() override;                                    // 由当前世界位置初始化起始角度
    void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;       // 写 radius/speed/height
    void onDeserialize(const nlohmann::json& in) override;      // 读 radius/speed/height

private:
    float m_angle = 0.0f;                                       // 当前环绕角（度），运行时状态不序列化
};

}  // namespace NovaGame
