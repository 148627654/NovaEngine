#pragma once
#include <glm/vec3.hpp>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// 能量球（§28.3.6/§28.3.11 脚本分工 4）：
// 每帧上浮摆动（sin）+ 绕 Y 自转；与玩家世界坐标 3D 距离轮询（拾取半径 1.8m，触发器无脚本回调）。
// 命中 → 子对象 Burst 粒子 play() → sfx_pickup → manager.notifyCollected() → 自身 setActiveSelf(false)。
class NeonCollectible : public NovaEngine::Behaviour {
public:
    explicit NeonCollectible(NovaEngine::GameObject* owner);

    float m_pickupRadius = 1.8f;   // 拾取半径（米）
    float m_bobHeight = 0.15f;     // 上浮摆动幅度（米）
    float m_bobSpeed = 2.0f;       // 摆动角速度（弧度/秒）
    float m_spinSpeedDeg = 60.0f;  // 自转角速度（度/秒）
    bool m_hidden = false;         // V1.2：隐藏晶体（不计入通关门，独立统计）

    void onStart() override;
    void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

private:
    // 运行期状态（不序列化）
    glm::vec3 m_basePos{0.0f};   // onStart 记录的初始位置（摆动基线）
    float m_phase = 0.0f;        // 摆动相位：按对象 id 派生，各球不同步（规格 §28.3.11 无此字段，故不入序列化）
    bool m_collected = false;    // 已拾取标记（防重复触发）
};

}  // namespace NovaGame
