#pragma once
#include <vector>
#include <glm/vec3.hpp>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// V1.2 幽灵回放：读取 saves/ghost.novaghost（最佳纪录的轨迹采样），
// 按当前对局用时回放位置——玩家与自己的最佳纪录比赛。
// 挂载：关卡场景 "Ghost" GO（初始 inactive；文件存在时脚本自激活）。
class NeonGhost : public NovaEngine::Behaviour {
public:
    explicit NeonGhost(NovaEngine::GameObject* owner);

    float sampleInterval = 0.1f;   // 采样间隔（秒；录制与回放共用）

    void onStart() override;
    void onUpdate(float dt) override;

private:
    struct Sample {
        float t = 0.0f;
        glm::vec3 pos{0.0f};
    };
    std::vector<Sample> m_samples;
    bool m_active = false;
};

}  // namespace NovaGame
