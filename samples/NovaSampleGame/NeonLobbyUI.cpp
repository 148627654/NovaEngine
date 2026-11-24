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

    // ===== 开始按钮（§28.3.5）：点击 → sfx → 传 level 参数 → 切关卡 =====
    if (NovaEngine::GameObject* btnGo = findOne("StartBtn")) {
        if (auto* btn = btnGo->getComponent<NovaEngine::Button>()) {
            if (mgr) {
                btn->onClick = [mgr]() {
                    mgr->playSfx("assets/audio/sfx_ui_click.wav", 1.0f);
                    NovaEngine::SceneManager::setSceneParam("level", R"({"index":1,"orbTotal":10})");
                    mgr->switchScene("NeonLevel1");
                };
            } else {
                NovaEngine::Logger::warn("NeonLobbyUI: 场景缺少 NeonGameManager（Game 对象），开始按钮未接线");
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
            std::string s = best >= 0.0f ? std::format("最佳纪录:{:.1f} 秒", best)
                                         : "最佳纪录:暂无";
            if (!boardLine.empty())
                s += std::format("\nTOP3: {}", boardLine);
            txt->text = s;
        }
    }

    // ===== 上次通关（§28.3.10 回传参数）：win=true 时显示并激活 LastRunText =====
    const std::string raw = NovaEngine::SceneManager::getSceneParam("lastResult");
    if (!raw.empty()) {
        try {
            const nlohmann::json j = nlohmann::json::parse(raw);
            bool win = false;
            float time = 0.0f;
            if (j.contains("win") && j["win"].is_boolean())
                win = j["win"].get<bool>();
            if (j.contains("time") && j["time"].is_number())
                time = j["time"].get<float>();
            if (win) {
                if (NovaEngine::GameObject* textGo = findOne("LastRunText")) {
                    textGo->setActiveSelf(true);
                    if (auto* txt = textGo->getComponent<NovaEngine::Text>())
                        txt->text = std::format("上次通关:{:.1f} 秒", time);
                }
            }
        } catch (const std::exception& e) {
            NovaEngine::Logger::warn("NeonLobbyUI: lastResult 参数解析失败({})，不显示上次通关", e.what());
        }
    }

    NovaEngine::Logger::info("NeonLobbyUI: 大厅接线完成（开始按钮/最佳纪录/上次通关）");
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonLobbyUI)
