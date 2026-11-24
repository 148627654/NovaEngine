#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaEngine {
class AudioClip;
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

    void stopBgm();             // 当前场景 "BGM" AudioSource → Stop()（fadeOutSeconds 生效，防爆音）
};

}  // namespace NovaGame
