#include "NeonLevelHud.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scene/Text.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <format>

namespace NovaGame {

NeonLevelHud::NeonLevelHud(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonLevelHud::onStart() {
    NovaEngine::Scene* scene = gameObject()->scene();
    if (!scene)
        return;

    // 按名缓存 UI 对象（§28.3.11：findGameObjectsByName，结果判空）
    auto cache = [scene](const std::string& name) -> NovaEngine::GameObject* {
        const auto found = scene->findGameObjectsByName(name);
        return found.empty() ? nullptr : found[0];
    };
    m_countText = cache("CountText");
    m_timerText = cache("TimerText");
    m_hintText = cache("HintText");
    m_comboText = cache("ComboText");   // V1.1 连击显示
    m_victoryRoot = cache("VictoryRoot");
    m_winTitle = cache("WinTitle");     // V1.1 评级
    m_winTime = cache("WinTime");
    m_winBest = cache("WinBest");

    NeonGameManager* mgr = NeonGameManager::instance();

    // RetryBtn：再玩一次（§28.3.6）——重传 level 参数 + 重进关卡
    if (NovaEngine::GameObject* btnGo = cache("RetryBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>(); btn && mgr) {
            btn->onClick = [mgr]() {
                mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                NovaEngine::SceneManager::setSceneParam("level", R"({"index":1,"orbTotal":10})");
                mgr->switchScene("NeonLevel1");
            };
        }
    }
    // LobbyBtn：返回大厅（§28.3.10 结果回传）——lastResult 参数 + 切大厅
    if (NovaEngine::GameObject* btnGo = cache("LobbyBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>(); btn && mgr) {
            btn->onClick = [mgr]() {
                mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                const std::string result = std::format(R"({{"win":true,"time":{:.1f}}})",
                                                       mgr->elapsedTime());
                NovaEngine::SceneManager::setSceneParam("lastResult", result);
                mgr->switchScene("NeonLobby");
            };
        }
    }

    NovaEngine::Logger::info("NeonLevelHud: HUD 引用缓存完成（Count/Timer/Hint {}，Victory {}，按钮 {}）",
                             m_countText && m_timerText && m_hintText ? "ok" : "缺失",
                             m_victoryRoot ? "ok" : "缺失",
                             mgr ? "已接线" : "缺 manager 未接线");
}

void NeonLevelHud::onUpdate(float /*dt*/) {
    if (!enabled)
        return;
    NeonGameManager* mgr = NeonGameManager::instance();
    if (!mgr)
        return;

    // ===== 每帧刷文案（§28.3.6 关卡流程）=====
    if (m_countText) {
        if (auto* txt = m_countText->getComponent<NovaEngine::Text>())
            txt->text = std::format("能量球 {} / {}", mgr->collected(), mgr->orbTotal());
    }
    if (m_timerText) {
        if (auto* txt = m_timerText->getComponent<NovaEngine::Text>())
            txt->text = std::format("时间 {:.1f} 秒", mgr->elapsedTime());
    }
    // ===== V1.1 Combo：连击 ≥2 显示，窗口结束/胜利清空 =====
    if (m_comboText) {
        if (auto* txt = m_comboText->getComponent<NovaEngine::Text>()) {
            const int combo = mgr->combo();
            const std::string s = (!mgr->won() && combo >= 2)
                                      ? std::format("连击 x{}!", combo)
                                      : std::string();
            if (txt->text != s)
                txt->text = s;
        }
    }
    // 提示文案：激活后切换（只在变化时重赋，避免每帧字符串分配）
    const std::string hint = mgr->gateActive() && !mgr->won()
                                 ? "传送门已激活!冲进传送门!"
                                 : "收集全部能量球,激活传送门!";
    if (m_hintText && hint != m_lastHint) {
        m_lastHint = hint;
        if (auto* txt = m_hintText->getComponent<NovaEngine::Text>())
            txt->text = hint;
    }

    // ===== 胜利上升沿：激活结算面板（§28.3.6；manager.finishRun 由 NeonGate 触发）=====
    if (mgr->won() && !m_victoryShown) {
        m_victoryShown = true;
        if (m_victoryRoot)
            m_victoryRoot->setActiveSelf(true);
        // ===== V1.1 评级：标题「通关! 评级 S」+ 评级配色 =====
        if (m_winTitle) {
            if (auto* txt = m_winTitle->getComponent<NovaEngine::Text>()) {
                if (mgr->rankName()[0] != '\0') {
                    txt->text = std::format("通关! 评级 {}", mgr->rankName());
                    txt->color = mgr->rankColor();
                }
            }
        }
        if (m_winTime) {
            if (auto* txt = m_winTime->getComponent<NovaEngine::Text>())
                txt->text = std::format("用时 {:.1f} 秒", mgr->elapsedTime());
        }
        if (m_winBest) {
            if (auto* txt = m_winBest->getComponent<NovaEngine::Text>())
                txt->text = std::format("最佳纪录:{:.1f} 秒", mgr->bestTime());
        }
        NovaEngine::Logger::info("NeonLevelHud: 胜利面板已激活");
    }
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonLevelHud)
