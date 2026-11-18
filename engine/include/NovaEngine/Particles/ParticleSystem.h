#pragma once
#include <cstdint>
#include <string>
#include <glm/vec4.hpp>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Scene/Component.h>

namespace NovaEngine {

// 粒子系统组件（V0.5-6 v1）：CPU 模拟粒子池 + GPU 实例化 billboard quad 单 draw call。
// Edit 态渲染「稳态快照」（按 emissionRate×lifetime 预填、时间冻结——参数变更当帧生效，
// 满足「参数编辑即时预览」）；Play 态 onUpdate 全速推进；退出 Play 快照恢复自动清零。
class ParticleSystem : public Component {
public:
    explicit ParticleSystem(GameObject* owner);

    float emissionRate = 30.0f;      // 个/秒
    float lifetime = 1.0f;           // 秒（±20% 随机）
    float initialSpeed = 2.0f;       // m/s
    float gravity = 0.0f;            // 世界 -Y 加速度（m/s²）
    glm::vec4 startColor{1.0f};
    glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};   // 寿命内线性插值
    float startSize = 0.2f;
    float endSize = 0.05f;
    std::string texturePath;         // 纹理条（flipbook）贴图；空 = 白块
    int tileX = 1, tileY = 1;        // 纹理条网格（寿命内按序播放）
    int maxParticles = 1000;
    bool loop = true;                // false = 播完停止发射
    bool playOnAwake = true;
    bool billboard = true;           // v1 固定 true（相机面向 quad）
    bool blendAdditive = false;      // true = 加性混合（火花/火焰）

    void play(); void stop(); void clear();          // 运行期控制（Play 中脚本可用）
    bool isPlaying() const;
    uint32_t aliveCount() const;

    void onUpdate(float dt) override;                // Play 推进（发射/积分/死亡回收）
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
