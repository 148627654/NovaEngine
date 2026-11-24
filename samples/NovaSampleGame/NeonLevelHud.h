#pragma once
#include <string>
#include <NovaEngine/Scripting/Behaviour.h>

namespace NovaEngine {
class GameObject;
}

namespace NovaGame {

// 关卡 HUD（§28.3.6 关卡流程）：
// onStart 按名缓存 UI 对象引用并给 RetryBtn/LobbyBtn 接 onClick；
// 每帧刷计数/计时/提示文案；胜利上升沿激活 VictoryRoot 并填 WinTime/WinBest。
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
    NovaEngine::GameObject* m_victoryRoot = nullptr;
    NovaEngine::GameObject* m_winTitle = nullptr;    // V1.1 评级标题
    NovaEngine::GameObject* m_winTime = nullptr;
    NovaEngine::GameObject* m_winBest = nullptr;

    bool m_victoryShown = false;   // 胜利面板只激活一次（上升沿）
    std::string m_lastHint;        // 提示文案变化检测（避免每帧重赋字符串）
};

}  // namespace NovaGame
