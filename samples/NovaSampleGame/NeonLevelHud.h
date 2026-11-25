#pragma once
#include <string>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaEngine {
class GameObject;
}

namespace NovaGame {

// 关卡 HUD（§28.3.6 关卡流程 + V1.3 §28.3.15.5 挑战/失败/toast）：
// onStart 按名缓存 UI 对象引用并给 RetryBtn/LobbyBtn/FailRetryBtn/FailLobbyBtn 接 onClick；
// 每帧刷计数/计时（挑战=剩余倒计时/≤10s 变红）/提示文案；胜利上升沿激活 VictoryRoot
// 并填 WinTime/WinBest（按模式）；失败上升沿激活 FailRoot；成就 toast 轮询消费。
class NeonLevelHud : public NovaEngine::Behaviour {
public:
    explicit NeonLevelHud(NovaEngine::GameObject* owner);

    void onStart() override;
    void onUpdate(float dt) override;

private:
    // 缓存的 UI 对象（运行期引用，不序列化——场景重载后随场景销毁）
    NovaEngine::GameObject* m_countText = nullptr;
    NovaEngine::GameObject* m_timerText = nullptr;
    NovaEngine::GameObject* m_hintText = nullptr;
    NovaEngine::GameObject* m_comboText = nullptr;   // V1.1 连击显示
    NovaEngine::GameObject* m_hiddenText = nullptr;  // V1.2 隐藏晶体计数
    NovaEngine::GameObject* m_victoryRoot = nullptr;
    NovaEngine::GameObject* m_winTitle = nullptr;    // V1.1 评级标题
    NovaEngine::GameObject* m_winTime = nullptr;
    NovaEngine::GameObject* m_winBest = nullptr;
    NovaEngine::GameObject* m_failRoot = nullptr;    // V1.3 失败面板（时间耗尽）
    NovaEngine::GameObject* m_toastText = nullptr;   // V1.3 成就解锁 toast

    bool m_victoryShown = false;   // 胜利面板只激活一次（上升沿）
    bool m_failShown = false;      // V1.3 失败面板只激活一次（上升沿）
    float m_toastRemaining = 0.0f; // V1.3 toast 显示剩余秒数（0 = 空闲）
    std::string m_lastHint;        // 提示文案变化检测（避免每帧重赋字符串）
};

}  // namespace NovaGame
