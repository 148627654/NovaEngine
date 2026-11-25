#include "NeonLevelHud.h"
#include "NeonAchievements.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scene/Text.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <algorithm>
#include <format>
#include <glm/vec4.hpp>

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
    m_hiddenText = cache("HiddenText"); // V1.2 隐藏晶体计数
    m_victoryRoot = cache("VictoryRoot");
    m_winTitle = cache("WinTitle");     // V1.1 评级
    m_winTime = cache("WinTime");
    m_winBest = cache("WinBest");
    m_failRoot = cache("FailRoot");     // V1.3 失败面板
    m_toastText = cache("ToastText");   // V1.3 成就 toast（初始 active false）

    NeonGameManager* mgr = NeonGameManager::instance();

    // RetryBtn：再玩一次（§28.3.6）——点击时读当前对局 mode 重传 level 参数（§28.3.15.7 分工 4）
    if (NovaEngine::GameObject* btnGo = cache("RetryBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>(); btn && mgr) {
            btn->onClick = [mgr]() {
                mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                const std::string level = std::format(
                    R"({{"index":1,"orbTotal":10,"mode":"{}"}})", mgr->mode());
                NovaEngine::SceneManager::setSceneParam("level", level);
                mgr->switchScene("NeonLevel1");
            };
        }
    }
    // LobbyBtn：返回大厅（§28.3.10 结果回传）——点击时读当前对局 mode 拼 lastResult
    if (NovaEngine::GameObject* btnGo = cache("LobbyBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>(); btn && mgr) {
            btn->onClick = [mgr]() {
                mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                std::string result;
                if (mgr->won()) {
                    result = std::format(R"({{"win":true,"time":{:.1f},"mode":"{}"}})",
                                         mgr->elapsedTime(), mgr->mode());
                    if (mgr->mode() == "challenge")
                        result = std::format(
                            R"({{"win":true,"time":{:.1f},"mode":"challenge","remaining":{:.1f}}})",
                            mgr->elapsedTime(), mgr->remaining());
                } else {
                    result = std::format(R"({{"win":false,"mode":"{}"}})", mgr->mode());
                }
                NovaEngine::SceneManager::setSceneParam("lastResult", result);
                mgr->switchScene("NeonLobby");
            };
        }
    }
    // FailRetryBtn：挑战失败再试一次（§28.3.15.5）——固定 challenge 参数重进关卡
    if (NovaEngine::GameObject* btnGo = cache("FailRetryBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>(); btn && mgr) {
            btn->onClick = [mgr]() {
                mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                NovaEngine::SceneManager::setSceneParam("level",
                                                        R"({"index":1,"orbTotal":10,"mode":"challenge"})");
                mgr->switchScene("NeonLevel1");
            };
        }
    }
    // FailLobbyBtn：挑战失败返回大厅（§28.3.15.5）——lastResult win=false
    if (NovaEngine::GameObject* btnGo = cache("FailLobbyBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>(); btn && mgr) {
            btn->onClick = [mgr]() {
                mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                NovaEngine::SceneManager::setSceneParam("lastResult", R"({"win":false,"mode":"challenge"})");
                mgr->switchScene("NeonLobby");
            };
        }
    }

    NovaEngine::Logger::info("NeonLevelHud: HUD 引用缓存完成（Count/Timer/Hint {}，Victory {}，Fail {}，"
                             "Toast {}，按钮 {}）",
                             m_countText && m_timerText && m_hintText ? "ok" : "缺失",
                             m_victoryRoot ? "ok" : "缺失",
                             m_failRoot ? "ok" : "缺失",
                             m_toastText ? "ok" : "缺失",
                             mgr ? "已接线" : "缺 manager 未接线");
}

void NeonLevelHud::onUpdate(float dt) {
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
        if (auto* txt = m_timerText->getComponent<NovaEngine::Text>()) {
            if (mgr->mode() == "challenge") {
                // ===== V1.3 限时挑战计时（§28.3.15.5）：「剩余 x.x 秒」；≤10s 红色告警 =====
                const float remaining = std::max(0.0f, mgr->remaining());
                const std::string s = std::format("剩余 {:.1f} 秒", remaining);
                if (txt->text != s)
                    txt->text = s;
                const glm::vec4 warnColor = remaining <= 10.0f
                                                ? glm::vec4(1.0f, 0.35f, 0.30f, 1.0f)
                                                : glm::vec4(0.9f, 0.95f, 1.0f, 1.0f);
                if (txt->color != warnColor)
                    txt->color = warnColor;
            } else {
                txt->text = std::format("时间 {:.1f} 秒", mgr->elapsedTime());
            }
        }
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
    // ===== V1.2 隐藏晶体计数（右上角计时下方）=====
    if (m_hiddenText) {
        if (auto* txt = m_hiddenText->getComponent<NovaEngine::Text>()) {
            const std::string s = std::format("隐藏晶体 {}/{}", mgr->hiddenCollected(),
                                              mgr->hiddenTotal());
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
        // ===== V1.1 评级：标题「通关! 评级 S」+ 评级配色（挑战评级本身按剩余，标题不变）=====
        if (m_winTitle) {
            if (auto* txt = m_winTitle->getComponent<NovaEngine::Text>()) {
                if (mgr->rankName()[0] != '\0') {
                    txt->text = std::format("通关! 评级 {}", mgr->rankName());
                    txt->color = mgr->rankColor();
                }
            }
        }
        if (m_winTime) {
            if (auto* txt = m_winTime->getComponent<NovaEngine::Text>()) {
                if (mgr->mode() == "challenge") {
                    // ===== V1.3 挑战版文案：用时 + 剩余合一行（§28.3.15.5）=====
                    txt->text = std::format("用时 {:.1f} 秒 · 剩余 {:.1f} 秒", mgr->elapsedTime(),
                                            mgr->remaining());
                } else {
                    // V1.2：用时 + 隐藏晶体（有则显）合一行
                    const bool allHidden = mgr->hiddenCollected() >= mgr->hiddenTotal();
                    txt->text = allHidden
                                    ? std::format("用时 {:.1f} 秒 · 隐藏晶体全收集!", mgr->elapsedTime())
                                    : std::format("用时 {:.1f} 秒", mgr->elapsedTime());
                }
            }
        }
        if (m_winBest) {
            if (auto* txt = m_winBest->getComponent<NovaEngine::Text>()) {
                if (mgr->mode() == "challenge") {
                    // ===== V1.3 挑战版最佳 = 限时最佳（leaderboard_challenge 首项，§28.3.15.5）=====
                    txt->text = std::format("限时最佳:剩余 {:.1f} 秒", mgr->bestRemaining());
                } else {
                    // ===== V1.2 最佳分段 =====
                    txt->text = std::format("最佳纪录:{:.1f} 秒 · 最佳分段:{:.1f} 秒",
                                            mgr->bestTime(), mgr->bestSplit());
                }
            }
        }
        NovaEngine::Logger::info("NeonLevelHud: 胜利面板已激活（mode={}）", mgr->mode());
    }

    // ===== V1.3 失败上升沿（§28.3.15.5）：时间耗尽 → 失败面板；胜利优先互斥 =====
    if (mgr->failed() && !m_failShown && !mgr->won()) {
        m_failShown = true;
        if (m_failRoot)
            m_failRoot->setActiveSelf(true);
        NovaEngine::Logger::info("NeonLevelHud: 失败面板已激活（时间耗尽）");
    }

    // ===== V1.3 成就 toast（§28.3.15.6）：显示中倒计时 → 归零隐藏；空闲且有队列 → 取队首显示 3s =====
    if (m_toastText) {
        NeonAchievements* ach = NeonAchievements::instance();
        if (m_toastRemaining > 0.0f) {
            m_toastRemaining -= dt;
            if (m_toastRemaining <= 0.0f) {
                m_toastRemaining = 0.0f;
                m_toastText->setActiveSelf(false);
                // 队首消息已在显示时 consumeToast() 出队；此处不再二次出队（否则会丢弃下一条）
            }
        } else if (ach && ach->pendingToast()) {
            const std::string msg = ach->consumeToast();
            if (auto* txt = m_toastText->getComponent<NovaEngine::Text>())
                txt->text = msg;
            m_toastText->setActiveSelf(true);
            m_toastRemaining = 3.0f;
        }
    }
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonLevelHud)
