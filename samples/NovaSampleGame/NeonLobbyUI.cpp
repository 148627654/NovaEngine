#include "NeonLobbyUI.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/FileUtil.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/SaveGame/SaveGame.h>
#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scene/Text.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <format>
#include <glm/vec4.hpp>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace NovaGame {

NeonLobbyUI::NeonLobbyUI(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonLobbyUI::onStart() {
    NovaEngine::Scene* scene = gameObject()->scene();
    if (!scene)
        return;

    // 按名查找唯一对象（结果判空后再取 [0]——防弹窗纪律）
    auto findOne = [scene](const std::string& name) -> NovaEngine::GameObject* {
        const auto found = scene->findGameObjectsByName(name);
        return found.empty() ? nullptr : found[0];
    };

    NeonGameManager* mgr = NeonGameManager::instance();

    // ===== 开始按钮（§28.3.5）：点击 → sfx → 传 level 参数（V1.3 显式 mode=classic）→ 切关卡 =====
    if (NovaEngine::GameObject* btnGo = findOne("StartBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>()) {
            if (mgr) {
                btn->onClick = [mgr]() {
                    mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                    NovaEngine::SceneManager::setSceneParam(
                        "level", R"({"index":1,"orbTotal":10,"mode":"classic"})");
                    mgr->switchScene("NeonLevel1");
                };
            } else {
                NovaEngine::Logger::warn("NeonLobbyUI: 场景缺少 NeonGameManager（Game 对象），开始按钮未接线");
            }
        }
    }

    // ===== V1.3 挑战按钮（§28.3.15.5）：mode=challenge 进限时挑战 =====
    if (NovaEngine::GameObject* btnGo = findOne("ChallengeBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>()) {
            if (mgr) {
                btn->onClick = [mgr]() {
                    mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                    NovaEngine::SceneManager::setSceneParam(
                        "level", R"({"index":1,"orbTotal":10,"mode":"challenge"})");
                    mgr->switchScene("NeonLevel1");
                };
            } else {
                NovaEngine::Logger::warn("NeonLobbyUI: 场景缺少 NeonGameManager（Game 对象），挑战按钮未接线");
            }
        }
    }

    // ===== 最佳纪录（§28.3.12）：listSlots 只读元数据，绝不 load（load 会原地重建场景摧毁大厅）=====
    float best = -1.0f;
    for (const auto& slot : NovaEngine::SaveGame::listSlots()) {
        if (slot.slot == 0 && slot.valid) {
            best = slot.playTimeSeconds;
            break;
        }
    }
    // ===== V1.2 本地排行榜 TOP3（saves/leaderboard.novaboard）=====
    std::string boardLine;
    std::string boardRaw;
    if (NovaEngine::FileUtil::readText("saves/leaderboard.novaboard", boardRaw)) {
        try {
            const nlohmann::json bj = nlohmann::json::parse(boardRaw);
            if (bj.is_array()) {
                for (size_t i = 0; i < bj.size() && i < 3; ++i)
                    boardLine += (i ? " / " : "") + std::format("{:.1f}", bj[i].get<float>());
            }
        } catch (...) { boardLine.clear(); }
    }
    if (NovaEngine::GameObject* textGo = findOne("BestTimeText")) {
        if (auto* txt = textGo->getComponent<NovaEngine::Text>()) {
            // V1.3 文案前缀「经典」（§28.3.15.5）
            std::string s = best >= 0.0f ? std::format("经典最佳:{:.1f} 秒", best)
                                         : "经典最佳:暂无";
            if (!boardLine.empty())
                s += std::format("\nTOP3: {}", boardLine);
            txt->text = s;
        }
    }

    // ===== V1.3 限时最佳（§28.3.15.5）：读 leaderboard_challenge 首项 + TOP3 =====
    std::string cbestLine;
    float cbest = -1.0f;
    std::string cboardRaw;
    if (NovaEngine::FileUtil::readText("saves/leaderboard_challenge.novaboard", cboardRaw)) {
        try {
            const nlohmann::json bj = nlohmann::json::parse(cboardRaw);
            if (bj.is_array() && !bj.empty() && bj[0].is_number()) {
                cbest = bj[0].get<float>();
                for (size_t i = 0; i < bj.size() && i < 3; ++i)
                    cbestLine += (i ? " / " : "") + std::format("{:.1f}", bj[i].get<float>());
            }
        } catch (...) { cbestLine.clear(); }
    }
    if (NovaEngine::GameObject* textGo = findOne("ChallengeBestText")) {
        if (auto* txt = textGo->getComponent<NovaEngine::Text>()) {
            std::string s = cbest >= 0.0f ? std::format("限时最佳:剩余 {:.1f} 秒", cbest)
                                          : "限时最佳:暂无";
            if (!cbestLine.empty())
                s += std::format("\nTOP3: {}", cbestLine);
            txt->text = s;
        }
    }

    // ===== 上次通关（§28.3.10 回传参数；V1.3 §28.3.15.5 按 lastResult.mode 选文案）=====
    const std::string raw = NovaEngine::SceneManager::getSceneParam("lastResult");
    if (!raw.empty()) {
        try {
            const nlohmann::json j = nlohmann::json::parse(raw);
            bool win = false;
            float time = 0.0f;
            float remaining = 0.0f;
            std::string mode = "classic";   // mode 缺失 → 默认 classic（向后兼容）
            if (j.contains("win") && j["win"].is_boolean())
                win = j["win"].get<bool>();
            if (j.contains("time") && j["time"].is_number())
                time = j["time"].get<float>();
            if (j.contains("remaining") && j["remaining"].is_number())
                remaining = j["remaining"].get<float>();
            if (j.contains("mode") && j["mode"].is_string())
                mode = j["mode"].get<std::string>();
            if (NovaEngine::GameObject* textGo = findOne("LastRunText")) {
                if (win) {
                    textGo->setActiveSelf(true);
                    if (auto* txt = textGo->getComponent<NovaEngine::Text>()) {
                        txt->text = (mode == "challenge")
                                        ? std::format("上次挑战:剩余 {:.1f} 秒", remaining)
                                        : std::format("上次通关:{:.1f} 秒", time);
                    }
                } else {
                    textGo->setActiveSelf(false);   // 失败回大厅：隐藏（协议只定义 win 文案）
                }
            }
        } catch (const std::exception& e) {
            NovaEngine::Logger::warn("NeonLobbyUI: lastResult 参数解析失败({})，不显示上次通关", e.what());
        }
    }

    // ===== V1.3 成就墙（§28.3.15.6）：onStart 直接 FileUtil 读文件填写，不依赖 NeonAchievements 单例 =====
    // 12 行固定顺序（与解锁 id 对齐）；未解锁置灰名称 + 条件，解锁金色
    static constexpr struct AchRow {
        const char* id;     // unlocked 键
        const char* name;   // 成就名
        const char* cond;   // 条件文案（§28.3.15.6 表逐字）
    } kAchTable[] = {
        {"first_clear", "初次通关", "第一次通关"},
        {"all_orbs", "满载而归", "一局集满全部能量球"},
        {"rank_s", "完美评级", "经典模式评级 S"},
        {"hidden_2", "晶体猎人", "一局收集 2 颗隐藏晶体"},
        {"deathless", "完美跑者", "一次不掉落通关"},
        {"challenge_clear", "分秒必争", "通关限时挑战"},
        {"challenge_s", "时间大师", "限时挑战评级 S"},
        {"combo_5", "连击大师", "连击达到 x5"},
        {"dash_50", "冲刺达人", "累计冲刺 50 次"},
        {"orb_200", "收集狂", "累计收集 200 颗能量球"},
        {"beat_ghost", "超越自我", "通关快于幽灵纪录"},
        {"fall_5", "摔跤艺术家", "掉落 5 次仍通关"},
    };
    std::unordered_set<std::string> unlockedIds;
    std::string achRaw;
    if (NovaEngine::FileUtil::readText("saves/achievements.novaachievements", achRaw)) {
        try {
            const nlohmann::json aj = nlohmann::json::parse(achRaw);
            if (aj.contains("unlocked") && aj["unlocked"].is_object())
                for (const auto& [k, v] : aj["unlocked"].items())
                    (void)v, unlockedIds.insert(k);
        } catch (...) { unlockedIds.clear(); }
    }
    if (NovaEngine::GameObject* titleGo = findOne("AchTitle")) {
        if (auto* txt = titleGo->getComponent<NovaEngine::Text>())
            txt->text = std::format("成就 {}/12", unlockedIds.size());
    }
    for (int i = 0; i < 12; ++i) {
        const std::string goName = std::format("Ach{:02}", i + 1);   // Ach01..Ach12
        if (NovaEngine::GameObject* textGo = findOne(goName)) {
            if (auto* txt = textGo->getComponent<NovaEngine::Text>()) {
                const bool unlocked = unlockedIds.contains(kAchTable[i].id);
                txt->text = std::format("{} {} · {}", unlocked ? "★" : "☆", kAchTable[i].name,
                                        kAchTable[i].cond);
                txt->color = unlocked ? glm::vec4(0.95f, 0.85f, 0.35f, 1.0f)
                                      : glm::vec4(0.45f, 0.48f, 0.55f, 1.0f);
            }
        }
    }

    NovaEngine::Logger::info("NeonLobbyUI: 大厅接线完成（开始/挑战按钮/经典最佳/限时最佳/上次通关/成就墙 {} 项）",
                             unlockedIds.size());
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonLobbyUI)
