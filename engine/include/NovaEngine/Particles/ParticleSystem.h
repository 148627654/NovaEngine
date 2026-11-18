#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Scene/Component.h>

namespace NovaEngine {

// 粒子实例数据（ParticleSystem → ParticleRenderer 消费，V0.5-6 模块内部约定）：
// 颜色/尺寸已按寿命插值（start→end）、tile 已钳制到 [0, tileX*tileY-1]；
// 渲染器直接并入实例 VBO（位置/颜色/尺寸/条索引，与 particle.vert 的 location 1..4 对齐）。
struct ParticleInstance {
    glm::vec3 position;
    glm::vec4 color;
    float size = 0.0f;
    float tile = 0.0f;
};

// 粒子系统组件（V0.5-6 v1）：CPU 模拟粒子池 + GPU 实例化 billboard quad 单 draw call。
// Edit 态渲染「稳态快照」（按 emissionRate×lifetime 确定性预填、时间冻结——参数变更当帧生效，
// 满足「参数编辑即时预览」）；Play 态 onUpdate 全速推进；退出 Play 经快照反序列化自动清零。
// 发射形状（v1 固定，无序列化字段——§25.4 冻结头未含形状参数）：以发射器 up 轴为轴、
// 半角 30° 圆锥内均匀随机方向；速度 = initialSpeed × [0.6, 1.0]；每粒子寿命 = lifetime × [0.8, 1.2]
//（契约 ±20%）。billboard 字段 v1 固定 true（相机面向 quad）。
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
    bool loop = true;                // false = 发射 lifetime 秒后停发，已发射粒子自然消亡
    bool playOnAwake = true;
    bool billboard = true;           // v1 固定 true（相机面向 quad）
    bool blendAdditive = false;      // true = 加性混合（火花/火焰）

    void play(); void stop(); void clear();          // 运行期控制（Play 中脚本可用）
    bool isPlaying() const;
    uint32_t aliveCount() const;

    void onStart() override;                         // 进入 Play：playOnAwake → play()
    void onUpdate(float dt) override;                // Play 推进（发射/积分/死亡回收）
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

    // ===== 渲染器消费接口（ParticleRenderer 专用；§25.4 冻结接口之外的追加，不涉及外部模块）=====
    // 进入过 Play 模拟（m_simulated）→ 实时池实例；否则（Edit 态 / Play 首帧前）→
    // 按当前参数确定性重建的稳态快照（每帧重建、时间冻结——参数变更当帧生效）。
    void collectInstances(std::vector<ParticleInstance>& out) const;

private:
    struct Particle {
        glm::vec3 position;
        glm::vec3 velocity;
        float age = 0.0f;       // 已存活秒数
        float lifetime = 1.0f;  // 本粒子寿命（lifetime × [0.8, 1.2]）
    };

    void advance(float dt);                              // 发射 + 积分 + 死亡回收
    void spawnParticle(const glm::vec3& origin, const glm::vec3& axis);
    std::vector<Particle> buildSteadyState() const;      // Edit 稳态快照（固定种子确定性分布）
    ParticleInstance instanceFrom(const Particle& p) const;  // 寿命插值 → 渲染实例

    std::vector<Particle> m_particles;
    bool m_playing = false;       // 发射中（play 置位；stop / loop=false 播完清零）
    bool m_simulated = false;     // 进入过 Play 模拟（onStart/onUpdate 置位）——渲染器据此选
                                  // 实时池或 Edit 稳态快照；快照反序列化（退出 Play）自然复位
    bool m_started = false;       // onStart 已调用（Play 中挂载的组件收不到 onStart 时补播用）
    float m_emitAccumulator = 0.0f;   // 发射速率小数积压（每帧按 emissionRate×dt 累计）
    float m_playTime = 0.0f;          // 本次 play() 起已发射时长（loop=false 停发判定）
    uint32_t m_rngState = 0;          // 实时模拟 LCG 状态（稳态快照另用固定种子）
};

}  // namespace NovaEngine
