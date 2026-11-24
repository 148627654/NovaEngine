#pragma once
#include <glm/vec3.hpp>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaEngine {
class CharacterController;
}

namespace NovaGame {

// 玩家看守（§28.3.6 复活/§28.3.11 脚本分工 6）：
// 掉落 y < respawnY → 复位出生点 + 清速度 + sfx_fall（已收集计数保留）；
// isGrounded() 下降沿且垂直速度向上 → sfx_jump（volume 0.5）。
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
};

}  // namespace NovaGame
