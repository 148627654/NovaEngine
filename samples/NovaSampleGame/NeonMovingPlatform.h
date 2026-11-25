#pragma once
#include <glm/vec3.hpp>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// V1.3 移动平台（§28.3.15.3 平台行为表）：
// 纯位置脚本——不做任何物理调用：kinematic Rigidbody 由引擎 writeKinematics
// 每固定步把 Transform 同步到物理 body（引擎条款 §28.3.15.4，对脚本透明），
// 本脚本只按 sin 周期写 Transform 位置。
// 运动公式：位置 = 基准 + sin(totalTime×2π/periodSec + 相位)×振幅×轴向。
// 挂载：MPA/MPB/MPC 根对象（axis 0=X / 1=Y / 2=Z）。
class NeonMovingPlatform : public NovaEngine::Behaviour {
public:
    explicit NeonMovingPlatform(NovaEngine::GameObject* owner);

    int m_axis = 0;              // 运动轴：0=X / 1=Y / 2=Z（§28.3.15.3 平台表）
    float m_amplitude = 1.0f;    // 振幅（米）
    float m_periodSec = 6.0f;    // 周期（秒）
    float m_phaseDeg = 0.0f;     // 相位（度）

    void onStart() override;
    void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

private:
    glm::vec3 m_base{0.0f};    // 基准位置（onStart 快照）
};

}  // namespace NovaGame
