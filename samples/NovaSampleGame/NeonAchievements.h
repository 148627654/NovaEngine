#pragma once
#include <deque>
#include <string>
#include <unordered_map>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaGame {

// V1.3 本地成就（§28.3.15.6 成就清单 + §28.3.15.7 分工 3）：
// 静态单例（§28.3.11 热重载自愈纪律：onUpdate 开头 s_instance != this 重新注册）。
// 挂载：NeonLevel1 "Game" 对象（第三个组件；大厅不挂——解锁全部发生在关卡）。
// onStart 读 saves/achievements.novaachievements + 快照本模式幽灵 best；
// onUpdate 每帧轮询 manager 状态做边沿检测（自维护 prev，场景重载新实例自然复位）。
// 解锁 → 原子写文件 + sfx_achieve + toast 队列（上限 8，超出丢最旧 warn）。
// stats 累计写盘 = 解锁时 + won/failed 上升沿（§28.3.15.2 裁定 9）。
class NeonAchievements : public NovaEngine::Behaviour {
public:
    explicit NeonAchievements(NovaEngine::GameObject* owner);

    static NeonAchievements* instance();

    // toast 队列 API（NeonLevelHud 轮询消费；每条显示 3.0s）
    bool pendingToast() const;
    std::string consumeToast();   // 队首出队；空返回 ""

    void onStart() override;
    void onUpdate(float dt) override;

private:
    static NeonAchievements* s_instance;

    // 存档状态（文件 {"version":1,"unlocked":{id:epoch},"stats":{clear,dash,fall,orb}}）
    std::unordered_map<std::string, long long> m_unlocked;   // 成就 id → 解锁 epoch 秒
    struct Stats {
        int clear = 0;   // 通关次数
        int dash = 0;    // 累计冲刺次数
        int fall = 0;    // 累计掉落次数
        int orb = 0;     // 累计收集普通球数
    };
    Stats m_stats;

    float m_ghostBest = -1.0f;   // 本模式幽灵 best（经典=用时/限时=剩余；-1=无文件不判定）

    // ===== 边沿检测自维护 prev 值（场景重载新实例自然复位）=====
    bool m_prevGate = false;
    bool m_prevWon = false;
    bool m_prevFailed = false;
    int m_prevHidden = 0;
    int m_prevMaxCombo = 0;
    int m_prevFalls = 0;
    int m_prevDashRun = 0;
    int m_prevCollected = 0;

    std::deque<std::string> m_toastQueue;   // toast 队列（上限 8）

    void unlock(const char* id, const char* displayName);   // 解锁：记录 + 写盘 + 音效 + toast
    void writeFile();                                       // 原子写 saves/achievements.novaachievements
};

}  // namespace NovaGame
