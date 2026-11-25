#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaEngine {
class AudioClip;
class MeshRenderer;
}

namespace NovaGame {

// 《霓虹收集者》全局单例（§28.3.11 脚本分工 1）：
// 计分/传送门激活/胜利结算/场景切换/音效/存档的中央状态机。
// 挂载：大厅与关卡场景的 "Game" 空对象（两场景各挂一个实例）。
// 静态单例纪律：onStart 注册、onUpdate 开头若 s_instance != this 重新注册（热重载自愈）。
class NeonGameManager : public NovaEngine::Behaviour {
public:
    explicit NeonGameManager(NovaEngine::GameObject* owner);

    static NeonGameManager* instance();   // 静态单例（场景无 Game 对象时为 nullptr）

    void onStart() override;
    void onUpdate(float dt) override;

    // ===== 玩法 API（§28.3.11 接口约定，逐字对齐）=====
    void notifyCollected();                                  // 收集 +1；集满 → 传送门激活
    float finishRun();                                       // 胜利结算（幂等）：停表/写档/sfx_win/BGM 淡出，返回用时
    void switchScene(const std::string& name);               // BGM 淡出 0.5s → 协程等待 → loadSceneAsync(name, false)
    void playSfx(const std::string& relPath, float volume);  // SFX 播放（spatialBlend=0，AudioClip 加载缓存）

    // ===== 状态查询（HUD/门/球每帧轮询）=====
    int collected() const { return m_collected; }
    int orbTotal() const { return m_orbTotal; }
    bool gateActive() const { return m_gateActive; }
    bool won() const { return m_won; }
    float elapsedTime() const;   // 秒：进行中 = 实时，胜利后 = 结算用时
    float bestTime() const { return m_bestTime; }   // min(旧最佳, 本次)
    // ===== V1.1 连击与评级 =====
    int combo() const { return m_combo; }
    const char* rankName() const { return m_rankName; }   // "S"/"A"/"B"/"C"/""
    const glm::vec4& rankColor() const { return m_rankColor; }
    // ===== V1.2 竞速 =====
    void notifyHiddenCollected();              // 隐藏晶体（不计入通关门，独立统计）
    int hiddenCollected() const { return m_hiddenCollected; }
    int hiddenTotal() const { return m_hiddenTotal; }
    float bestSplit() const { return m_bestSplit; }   // 本次最佳分段（球到球用时）
    // ===== V1.3 限时挑战 / 成就统计 =====
    const std::string& mode() const { return m_mode; }   // "classic"/"challenge"（§28.3.15.2 裁定 3）
    bool failed() const { return m_failed; }
    float remaining() const { return m_remaining; }
    int maxCombo() const { return m_maxCombo; }
    int fallsRun() const { return m_fallsRun; }
    int dashRun() const { return m_dashRun; }
    float bestRemaining() const { return m_bestRemaining; }   // 限时最佳剩余（leaderboard_challenge 首项）
    void notifyFall();                         // 掉落 +1（成就 stats.fall 用）
    void notifyDash();                         // 冲刺 +1（成就 stats.dash 用）

private:
    static NeonGameManager* s_instance;

    // ===== 运行期状态（不序列化——场景重载即重开一局）=====
    int m_collected = 0;
    int m_orbTotal = 10;        // 缺省 10（§28.3.10：参数缺失/坏 JSON → 默认值，直接启动 Player 也能玩）
    int m_levelIndex = 1;       // 扩展位（index=2 预留给第 2 关，本波不用）
    bool m_gateActive = false;
    bool m_won = false;
    float m_startTime = 0.0f;   // 计时起点（Time::totalTime，onStart 记）
    float m_endTime = 0.0f;     // 胜利停表时刻
    float m_bestTime = 0.0f;    // 结算后的最佳纪录
    std::unordered_map<std::string, std::shared_ptr<NovaEngine::AudioClip>> m_sfxCache;   // SFX 加载缓存

    // ===== V1.1 连击/评级/维度渐亮状态（运行时，不序列化）=====
    int m_combo = 0;                  // 当前连击数
    float m_comboTimer = 0.0f;        // 连击窗口剩余（秒）
    const char* m_rankName = "";      // 通关评级 S/A/B/C
    glm::vec4 m_rankColor{1.0f, 0.85f, 0.3f, 1.0f};
    std::vector<NovaEngine::MeshRenderer*> m_strips;   // 路线灯带（收集后逐条点亮）
    glm::vec3 m_baseAmbient{0.0f}, m_baseFog{0.0f};    // 维度渐亮基准（onStart 快照）
    bool m_envSnapped = false;

    // ===== V1.2 竞速状态（运行时，不序列化）=====
    int m_hiddenCollected = 0;                // 隐藏晶体已收集
    int m_hiddenTotal = 2;                    // 隐藏晶体总数（§28.3.15.2 裁定 7：onStart 按场景统计覆盖，0 → 兜底 2）
    float m_bestSplit = 0.0f;                 // 本次最佳分段
    float m_recAccum = 0.0f;                  // 幽灵采样计时累积
    std::vector<std::pair<float, glm::vec3>> m_recording;   // 幽灵轨迹（t, pos）
    std::vector<float> m_splitTimes;          // 每颗晶体收集时刻（分段计时）

    // ===== V1.3 挑战/成就状态（运行时，不序列化）=====
    std::string m_mode = "classic";           // 关卡模式（level 参数 mode 字段，仅 "challenge" 生效）
    float m_remaining = 60.0f;                // 限时剩余（秒；普通球 +4 / 隐藏晶体 +6）
    bool m_failed = false;                    // 时间耗尽失败（守卫结算，双保险）
    int m_maxCombo = 0;                       // 本局最高连击（成就 combo_5 用）
    int m_fallsRun = 0;                       // 本局掉落次数（成就 deathless/fall_5 + stats.fall 用）
    int m_dashRun = 0;                        // 本局冲刺次数（成就 stats.dash 用）
    float m_bestRemaining = 0.0f;             // 限时最佳剩余（结算后 = max(旧首项, 本次剩余)）

    void stopBgm();             // 当前场景 "BGM" AudioSource → Stop()（fadeOutSeconds 生效，防爆音）
    void updateDimensionLight(float dt);   // V1.1 维度突破：环境随收集渐亮 + 灯带点亮
};

}  // namespace NovaGame
