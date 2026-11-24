#pragma once
#include <vector>
#include <glm/vec3.hpp>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaEngine {
class CharacterController;
class GameObject;
}

namespace NovaGame {

// 玩家看守（§28.3.6 复活/§28.3.11 脚本分工 6 + V1.1 扩展）：
// 掉落 y < respawnY → 复位检查点 + 清速度 + sfx_fall（已收集计数保留）；
// isGrounded() 下降沿且垂直速度向上 → sfx_jump（volume 0.5）；空中二段跳（vy 单帧跃升）→ sfx_jump；
// V1.1 检查点：站上新平台（沿路线 -z 前进）即更新复活点；Dash（水平速度 >8 边沿）→ sfx_dash + 拖尾粒子。
// 挂载：玩家 root（与 CharacterController/CharacterWalk 同对象）。
class NeonPlayerWatcher : public NovaEngine::Behaviour {
public:
    explicit NeonPlayerWatcher(NovaEngine::GameObject* owner);

    float m_respawnY = -10.0f;                    // 掉落复活阈值（米）
    glm::vec3 m_respawnPos{0.0f, 0.9f, 10.0f};    // 出生点（两场景 JSON 各写各的：大厅 (0,0.9,2)、关卡 (0,0.9,10)）

    void onStart() override;
    void onUpdate(float dt) override;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

private:
    NovaEngine::CharacterController* m_cc = nullptr;   // 运行期缓存（onStart 取）
    bool m_wasGrounded = false;                        // 上一帧着地状态（下降沿检测）
    // ===== V1.1 扩展状态（运行时，不序列化）=====
    std::vector<NovaEngine::GameObject*> m_pads;       // 检查点平台列表（onStart 缓存）
    NovaEngine::GameObject* m_dashTrail = nullptr;     // Dash 拖尾粒子（Player 子对象）
    float m_prevVy = 0.0f;                             // 上一帧垂直速度（二段跳音效判定）
    float m_prevHorizSpeed = 0.0f;                     // 上一帧水平速率（Dash 边沿判定）
};

}  // namespace NovaGame
