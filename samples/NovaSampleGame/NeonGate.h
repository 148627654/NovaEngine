#pragma once
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaEngine {
class GameObject;
}

namespace NovaGame {

// 终点传送门（§28.3.6/§28.3.11 脚本分工 5）：
// 每帧轮询 manager.gateActive() 上升沿 → 激活序列（门三对象材质换 neon_magenta 图 +
// GateAura 粒子 play() + sfx_gate_open）；激活后轮询胜利区（|x|≤3、-34.5≤z≤-29.5、
// 1.9≤y≤3.5，即站上 GatePad）→ manager.finishRun() 胜利结算。
class NeonGate : public NovaEngine::Behaviour {
public:
    explicit NeonGate(NovaEngine::GameObject* owner);

    float m_zoneHalfX = 3.0f;    // 胜利区 |x| 半径（米）
    float m_zoneZMin = -34.5f;   // 胜利区 Z 下限（米）
    float m_zoneZMax = -29.5f;   // 胜利区 Z 上限（米）
    float m_zoneYMin = 1.9f;     // 胜利区 Y 下限（米，站上 GatePad 顶面 1.5 之上）
    float m_zoneYMax = 3.5f;     // 胜利区 Y 上限（米）

    void onStart() override;
    void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

private:
    // 门三对象 + 粒子对象（onStart 按名缓存；运行期不序列化）。
    // 对象名约定（Levels 场景 JSON 必须照此命名）：GatePillarL / GatePillarR / GateBeam / GateAura
    NovaEngine::GameObject* m_pillarL = nullptr;
    NovaEngine::GameObject* m_pillarR = nullptr;
    NovaEngine::GameObject* m_beam = nullptr;
    NovaEngine::GameObject* m_aura = nullptr;
    bool m_activated = false;   // 激活只执行一次（上升沿）
};

}  // namespace NovaGame
