#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// V1.0-3 验收演示脚本（波 2 #3）：InputMap 轴驱动 CharacterController——
// W/S（Vertical）+ A/D（Horizontal）世界系移动，Space 跳跃（isGrounded 落地后才能再跳）。
// 每帧 setVelocity 覆盖水平速度（下一固定步消费）；垂直速度保留物理积分值——
// 重力/楼梯步进/坡度限制全部由 PhysicsWorld 的 CharacterVirtual 驱动，脚本不碰重力。
class CharacterWalk : public NovaEngine::Behaviour {
public:
    explicit CharacterWalk(NovaEngine::GameObject* owner);
    float m_speed = 4.0f;      // 米/秒，Inspector 可调
    float m_jumpSpeed = 6.0f;  // 起跳初速（米/秒），Inspector 可调
    void onStart() override;   // 挂载诊断日志（CC 是否找到）
    void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;   // 写 speed/jumpSpeed
    void onDeserialize(const nlohmann::json& in) override;  // 读 speed/jumpSpeed

private:
    float m_logAccum = 0.0f;   // 输入日志限频累积（运行时状态，不序列化）
    bool m_jumpLatched = false;  // 起跳锁存（波 3 #10 修复高帧率吞跳；运行时状态，不序列化）
    float m_jumpLatchTime = 0.0f;  // 锁存计时（超时兜底解锁）
    // ===== V1.1 动作系统（运行时状态，不序列化）=====
    int m_jumpCount = 0;         // 已用跳跃数（着地重置；二段跳上限 2）
    float m_dashTime = 0.0f;     // 冲刺剩余时间（秒）
    float m_dashCooldown = 0.0f; // 冲刺冷却剩余（秒）
};

}  // namespace NovaGame
