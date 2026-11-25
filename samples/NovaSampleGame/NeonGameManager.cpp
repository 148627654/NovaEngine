#include "NeonGameManager.h"

#include <NovaEngine/Audio/AudioClip.h>
#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Core/FileUtil.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/SaveGame/SaveGame.h>
#include <NovaEngine/Scene/AudioSource.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <nlohmann/json.hpp>

namespace NovaGame {

// 切场景协程（自由函数形式）：参数经标准协程参数复制进帧。
// 波 3 冒烟实测：MSVC 调试版下「lambda 协程 + 闭包捕获」的闭包未随帧复制——
// 恢复时捕获读 0xCC 未初始化填充（字符串捕获 → length_error，指针捕获 → 段错误），
// 无捕获 lambda 正常；自由函数参数复制路径不受影响（KNOWN-ISSUES 备案）。
static NovaEngine::Coroutine switchSceneAfterDelay(std::string name) {
    co_await NovaEngine::WaitForSeconds{0.5f};
    if (!NovaEngine::SceneManager::loadSceneAsync(name, false))
        NovaEngine::Logger::error("NeonGameManager: loadSceneAsync(\"{}\") 失败（场景名不在构建列表）",
                                  name);
}

NeonGameManager* NeonGameManager::s_instance = nullptr;

NeonGameManager::NeonGameManager(NovaEngine::GameObject* owner) : Behaviour(owner) {}

NeonGameManager* NeonGameManager::instance() { return s_instance; }

void NeonGameManager::onStart() {
    // 注册单例 + 重置本局状态（场景重载后 onStart 重跑 = 新一局）
    s_instance = this;
    m_collected = 0;
    m_gateActive = false;
    m_won = false;
    m_startTime = NovaEngine::Time::totalTime();
    m_combo = 0;
    m_comboTimer = 0.0f;
    m_rankName = "";
    m_strips.clear();
    m_envSnapped = false;
    m_hiddenCollected = 0;
    m_hiddenTotal = 2;   // 兜底值（场景统计 ≥1 时下方覆盖）
    m_bestSplit = 0.0f;
    m_recAccum = 0.0f;
    m_recording.clear();
    m_splitTimes.clear();
    // ===== V1.3 挑战/成就状态重置 =====
    m_mode = "classic";
    m_remaining = 60.0f;
    m_failed = false;
    m_maxCombo = 0;
    m_fallsRun = 0;
    m_dashRun = 0;
    m_bestRemaining = 0.0f;

    // 读关卡参数（§28.3.10：缺失/坏 JSON → 默认 index=1、orbTotal=10；§28.3.15.5：mode 仅 "challenge" 生效）
    const std::string raw = NovaEngine::SceneManager::getSceneParam("level");
    if (!raw.empty()) {
        try {
            const nlohmann::json j = nlohmann::json::parse(raw);
            if (j.contains("index") && j["index"].is_number_integer())
                m_levelIndex = j["index"].get<int>();
            if (j.contains("orbTotal") && j["orbTotal"].is_number_integer())
                m_orbTotal = j["orbTotal"].get<int>();
            if (j.contains("mode") && j["mode"].is_string() && j["mode"] == "challenge")
                m_mode = "challenge";
        } catch (const std::exception& e) {
            NovaEngine::Logger::warn("NeonGameManager: level 参数解析失败({})，回退默认 index=1、orbTotal=10",
                                     e.what());
            m_levelIndex = 1;
            m_orbTotal = 10;
        }
    }
    // ===== V1.1 维度突破：缓存路线灯带 + 环境基准 =====
    if (NovaEngine::Scene* scene = gameObject()->scene()) {
        for (int i = 1; i <= 13; ++i) {
            const std::string n = "Strip" + std::string(i < 10 ? "0" : "") + std::to_string(i);
            const auto found = scene->findGameObjectsByName(n);
            if (!found.empty())
                if (auto* mr = found[0]->getComponent<NovaEngine::MeshRenderer>())
                    m_strips.push_back(mr);
        }
        m_baseAmbient = scene->ambientColor;
        m_baseFog = scene->fogColor;
        m_envSnapped = true;
        // ===== V1.3 隐藏晶体总数场景统计（§28.3.15.2 裁定 7）：名称前缀 "HiddenOrb" 计数，
        // 统计 ≥1 才覆盖，否则兜底 2（大厅无 HiddenOrb 时保持默认）=====
        int hiddenCount = 0;
        scene->forEachObject([&hiddenCount](NovaEngine::GameObject* go) {
            if (go->name().rfind("HiddenOrb", 0) == 0)
                ++hiddenCount;
        });
        if (hiddenCount >= 1)
            m_hiddenTotal = hiddenCount;
    }
    NovaEngine::Logger::info("NeonGameManager: 单例已注册，关卡 index={} orbTotal={} mode={}（计时起点 {:.2f}s），"
                             "灯带 {} 根，隐藏晶体总数 {}",
                             m_levelIndex, m_orbTotal, m_mode, m_startTime, m_strips.size(),
                             m_hiddenTotal);
}

void NeonGameManager::onUpdate(float dt) {
    // 热重载自愈（§28.3.11）：DLL 重载后旧 s_instance 指针失效，重新认领
    if (s_instance != this) {
        s_instance = this;
        NovaEngine::Logger::info("NeonGameManager: 检测到单例失效，已重新注册（热重载自愈）");
    }

    // ===== V1.1 连击窗口衰减 =====
    if (m_comboTimer > 0.0f) {
        m_comboTimer -= dt;
        if (m_comboTimer <= 0.0f)
            m_combo = 0;
    }

    // ===== V1.1 维度突破：世界随收集渐亮 =====
    updateDimensionLight(dt);

    // ===== V1.3 限时挑战倒计时（§28.3.15.5）：挑战且未胜未败 → 扣时间；耗尽 → 失败 =====
    if (m_mode == "challenge" && !m_won && !m_failed) {
        m_remaining -= dt;
        if (m_remaining <= 0.0f) {
            m_remaining = 0.0f;
            m_failed = true;
            playSfx("assets/audio/sfx_fail.wav", 1.0f);
            stopBgm();
            NovaEngine::Logger::info("NeonGameManager: 时间耗尽 → 挑战失败（剩余 {:.1f}s），已停表",
                                     m_remaining);
        }
    }

    // ===== V1.2 幽灵轨迹录制（0.1s 采样；胜利/失败后停）=====
    if (!m_won && !m_failed) {
        m_recAccum += dt;
        if (m_recAccum >= 0.1f) {
            m_recAccum = 0.0f;
            if (NovaEngine::Scene* scene = gameObject()->scene()) {
                const auto players = scene->findGameObjectsByName("Player");
                if (!players.empty())
                    m_recording.emplace_back(elapsedTime(),
                                             players[0]->transform()->worldPosition());
            }
        }
    }
}

void NeonGameManager::notifyHiddenCollected() {
    // ===== V1.3 结算后状态冻结（Reviewer P2-2）：won/failed 后一切玩法状态不再变更 =====
    if (m_won || m_failed)
        return;
    ++m_hiddenCollected;
    if (m_mode == "challenge")
        m_remaining += 6.0f;   // §28.3.15.5：隐藏晶体 +6s
    playSfx("assets/audio/sfx_pickup3.wav", 1.0f);
    NovaEngine::Logger::info("NeonGameManager: 隐藏晶体 {}/{}（剩余 {:.1f}s）", m_hiddenCollected,
                             m_hiddenTotal, m_remaining);
}

void NeonGameManager::notifyFall() {
    // ===== V1.3 结算后状态冻结（Reviewer P2-2）：结算后掉落不计入本局统计 =====
    if (m_won || m_failed)
        return;
    ++m_fallsRun;
}

void NeonGameManager::notifyDash() {
    // ===== V1.3 结算后状态冻结（Reviewer P2-2）：结算后冲刺不计入累计统计 =====
    if (m_won || m_failed)
        return;
    ++m_dashRun;
}

void NeonGameManager::updateDimensionLight(float dt) {
    if (!m_envSnapped || m_won)
        return;
    NovaEngine::Scene* scene = gameObject()->scene();
    if (!scene)
        return;
    // 收集进度 0..1：环境亮度/雾色向「修复完成」目标平滑过渡（每帧指数逼近）
    const float t = m_orbTotal > 0
                        ? std::clamp(static_cast<float>(m_collected) / m_orbTotal, 0.0f, 1.0f)
                        : 0.0f;
    const float k = 1.0f - std::exp(-dt * 2.5f);   // 平滑系数
    const glm::vec3 targetAmbient = m_baseAmbient * 2.4f;
    const glm::vec3 targetFog = m_baseFog + glm::vec3(0.04f, 0.05f, 0.08f);
    scene->ambientColor += (targetAmbient - scene->ambientColor) * (k * t);
    scene->fogColor += (targetFog - scene->fogColor) * (k * t);
}

void NeonGameManager::notifyCollected() {
    // ===== V1.3 结算后状态冻结（Reviewer P2-2）：won/failed 后不再计数/加时/亮灯带/激活门 =====
    if (m_won || m_failed)
        return;
    ++m_collected;

    // ===== V1.2 分段计时：记录每颗晶体收集时刻 =====
    m_splitTimes.push_back(elapsedTime());

    // ===== V1.3 限时挑战：普通球 +4s（§28.3.15.5；上方守卫保证 !won && !failed 才可达）=====
    if (m_mode == "challenge")
        m_remaining += 4.0f;

    // ===== V1.1 Combo：2.5s 窗口内连续收集，音效逐级升高（3 档音高变体）=====
    ++m_combo;
    m_comboTimer = 2.5f;
    m_maxCombo = std::max(m_maxCombo, m_combo);   // V1.3 本局最高连击（成就 combo_5 用）
    const int variant = std::min(m_combo - 1, 2);
    const char* pickupSfx = variant == 0 ? "assets/audio/sfx_pickup.wav"
                            : variant == 1 ? "assets/audio/sfx_pickup2.wav"
                                           : "assets/audio/sfx_pickup3.wav";
    playSfx(pickupSfx, 1.0f);

    // ===== V1.1 维度突破：对应灯带点亮（世界随收集恢复能量）=====
    if (m_collected >= 1 && m_collected <= static_cast<int>(m_strips.size()))
        m_strips[static_cast<size_t>(m_collected) - 1]->material.emissionIntensity = 2.0f;

    NovaEngine::Logger::info("NeonGameManager: 收集能量球 {}/{}（连击 x{}）", m_collected,
                             m_orbTotal, m_combo);
    if (m_collected >= m_orbTotal && !m_gateActive) {
        m_gateActive = true;
        NovaEngine::Logger::info("NeonGameManager: 能量球全部收集完成，终点传送门激活!");
    }
}

float NeonGameManager::elapsedTime() const {
    return m_won ? (m_endTime - m_startTime)
                 : (NovaEngine::Time::totalTime() - m_startTime);
}

float NeonGameManager::finishRun() {
    if (m_won)
        return elapsedTime();   // 幂等：重复触发直接返回结算值
    if (m_failed)
        return elapsedTime();   // V1.3 失败守卫：时间耗尽后误入胜利区不结算（与 NeonGate 双保险）

    m_won = true;
    m_endTime = NovaEngine::Time::totalTime();
    const float elapsed = m_endTime - m_startTime;

    // ===== V1.2 最佳分段（相邻晶体间隔的最小值；两模式通用）=====
    for (size_t i = 0; i + 1 < m_splitTimes.size(); ++i) {
        const float seg = m_splitTimes[i + 1] - m_splitTimes[i];
        if (m_bestSplit <= 0.0f || seg < m_bestSplit)
            m_bestSplit = seg;
    }

    if (m_mode == "challenge") {
        // ===== V1.3 限时挑战结算（§28.3.15.5）：不写 slot 0；评级按剩余；独立榜单/幽灵 =====
        const float remaining = m_remaining;

        // 评级按剩余时间：S ≥40 / A ≥25 / B ≥10 / C >0（§28.3.15.2 裁定 5）
        if (remaining >= 40.0f) {
            m_rankName = "S";
            m_rankColor = glm::vec4(1.0f, 0.85f, 0.3f, 1.0f);   // 金
        } else if (remaining >= 25.0f) {
            m_rankName = "A";
            m_rankColor = glm::vec4(0.4f, 1.0f, 1.0f, 1.0f);    // 青
        } else if (remaining >= 10.0f) {
            m_rankName = "B";
            m_rankColor = glm::vec4(0.5f, 1.0f, 0.5f, 1.0f);    // 绿
        } else {
            m_rankName = "C";
            m_rankColor = glm::vec4(0.75f, 0.78f, 0.85f, 1.0f); // 灰
        }

        // 独立榜单 leaderboard_challenge（剩余降序 TOP5；§28.3.15.2 裁定 4）
        std::vector<float> board;
        std::string old;
        if (NovaEngine::FileUtil::readText("saves/leaderboard_challenge.novaboard", old)) {
            try {
                for (const auto& v : nlohmann::json::parse(old))
                    if (v.is_number())
                        board.push_back(v.get<float>());
            } catch (...) { board.clear(); }
        }
        const float oldBest = board.empty() ? -1.0f : board[0];
        // 幽灵 ghost_challenge：刷新纪录才写（剩余 > 旧首项或文件空，同 V1.2 语义）
        if (board.empty() || remaining > oldBest) {
            nlohmann::json ghost;
            ghost["best"] = remaining;
            nlohmann::json samples = nlohmann::json::array();
            for (const auto& [t, p] : m_recording)
                samples.push_back({t, p.x, p.y, p.z});
            ghost["samples"] = std::move(samples);
            if (!NovaEngine::FileUtil::writeText("saves/ghost_challenge.novaghost", ghost.dump()))
                NovaEngine::Logger::warn("NeonGameManager: 限时幽灵轨迹写入失败");
        }
        board.push_back(remaining);
        std::sort(board.begin(), board.end(), std::greater<float>());   // 剩余降序
        if (board.size() > 5)
            board.resize(5);
        nlohmann::json out = board;
        NovaEngine::FileUtil::writeText("saves/leaderboard_challenge.novaboard", out.dump());
        m_bestRemaining = std::max(oldBest, remaining);
    } else {
        // ===== 经典分支完全照旧（§28.3.15.7 分工 1）=====
        // 最佳纪录 = min(旧最佳, 本次)（§28.3.12：写档前先读槽 0 只读元数据）
        float oldBest = elapsed;
        for (const auto& slot : NovaEngine::SaveGame::listSlots()) {
            if (slot.slot == 0 && slot.valid) {
                oldBest = slot.playTimeSeconds;
                break;
            }
        }
        m_bestTime = std::min(oldBest, elapsed);

        // ===== V1.2 幽灵落盘：刷新纪录才写（保留最快一局的轨迹）+ 本地排行榜 TOP5 =====
        if (elapsed < oldBest) {
            nlohmann::json ghost;
            ghost["best"] = elapsed;
            nlohmann::json samples = nlohmann::json::array();
            for (const auto& [t, p] : m_recording)
                samples.push_back({t, p.x, p.y, p.z});
            ghost["samples"] = std::move(samples);
            if (!NovaEngine::FileUtil::writeText("saves/ghost.novaghost", ghost.dump()))
                NovaEngine::Logger::warn("NeonGameManager: 幽灵轨迹写入失败");
        }
        {
            // 排行榜：读旧表 → 追加本次 → 排序取 TOP5 → 原子写回
            std::vector<float> board;
            std::string old;
            if (NovaEngine::FileUtil::readText("saves/leaderboard.novaboard", old)) {
                try {
                    for (const auto& v : nlohmann::json::parse(old))
                        if (v.is_number())
                            board.push_back(v.get<float>());
                } catch (...) { board.clear(); }
            }
            board.push_back(elapsed);
            std::sort(board.begin(), board.end());
            if (board.size() > 5)
                board.resize(5);
            nlohmann::json out = board;
            NovaEngine::FileUtil::writeText("saves/leaderboard.novaboard", out.dump());
        }

        // ===== V1.1 评级（S < 20s / A < 30s / B < 45s / C 其余）=====
        if (elapsed < 20.0f) {
            m_rankName = "S";
            m_rankColor = glm::vec4(1.0f, 0.85f, 0.3f, 1.0f);   // 金
        } else if (elapsed < 30.0f) {
            m_rankName = "A";
            m_rankColor = glm::vec4(0.4f, 1.0f, 1.0f, 1.0f);    // 青
        } else if (elapsed < 45.0f) {
            m_rankName = "B";
            m_rankColor = glm::vec4(0.5f, 1.0f, 0.5f, 1.0f);    // 绿
        } else {
            m_rankName = "C";
            m_rankColor = glm::vec4(0.75f, 0.78f, 0.85f, 1.0f); // 灰
        }

        // 通关写档 slot 0（§28.3.12：原子写 saves/slot0.novasave；限时挑战不写——§28.3.15.2 裁定 6）
        if (NovaEngine::Scene* scene = gameObject()->scene())
            NovaEngine::SaveGame::save(*scene, 0, "assets/scenes/NeonLevel1.nova.json", elapsed);
    }

    // ===== V1.1 维度突破：通关瞬间全部灯带点亮（世界修复完成）=====
    for (NovaEngine::MeshRenderer* mr : m_strips)
        mr->material.emissionIntensity = 2.0f;

    playSfx("assets/audio/sfx_win.wav", 1.0f);
    stopBgm();   // 胜利画面：BGM 按 fadeOutSeconds 淡出
    if (m_mode == "challenge")
        NovaEngine::Logger::info(
            "NeonGameManager: 挑战通关! 用时 {:.1f} 秒，剩余 {:.1f} 秒，评级 {}，限时最佳 {:.1f} 秒（不写 slot 0）",
            elapsed, m_remaining, m_rankName, m_bestRemaining);
    else
        NovaEngine::Logger::info("NeonGameManager: 通关! 用时 {:.1f} 秒，最佳纪录 {:.1f} 秒（已写存档 slot 0）",
                                 elapsed, m_bestTime);
    return elapsed;
}

void NeonGameManager::switchScene(const std::string& name) {
    NovaEngine::Logger::info("NeonGameManager: 切换场景 → \"{}\"（BGM 淡出 0.5s 后异步加载）", name);
    stopBgm();
    // 协程防爆音（§28.3.2 裁定 12）：Stop 后等 0.5s（AudioSource 内部按 fadeOutSeconds 淡出到期停）
    // 再 loadSceneAsync；协程挂在 manager 自身 Behaviour 上，随 Scene::update 推进。
    startCoroutine(switchSceneAfterDelay(name));
}

void NeonGameManager::playSfx(const std::string& relPath, float volume) {
    if (!NovaEngine::AudioEngine::available())
        return;
    // AudioClip 加载缓存（避免每次拾取重复解码 wav；失败缓存 nullptr 防每帧重试）
    auto it = m_sfxCache.find(relPath);
    if (it == m_sfxCache.end()) {
        auto clip = NovaEngine::AudioClip::load(relPath);
        if (!clip) {
            NovaEngine::Logger::warn("NeonGameManager: 音效加载失败 \"{}\"（跳过播放）", relPath);
            m_sfxCache[relPath] = nullptr;
            return;
        }
        it = m_sfxCache.emplace(relPath, clip).first;
    }
    if (!it->second)
        return;
    // spatialBlend=0：2D 全音量（§28.3.2 裁定 11）；位置传原点即可
    NovaEngine::AudioEngine::play(it->second, glm::vec3(0.0f), volume, false, 0.0f, 1.0f, 50.0f);
}

void NeonGameManager::stopBgm() {
    NovaEngine::Scene* scene = gameObject()->scene();
    if (!scene)
        return;
    const auto bgms = scene->findGameObjectsByName("BGM");
    if (bgms.empty())
        return;
    if (auto* src = bgms[0]->getComponent<NovaEngine::AudioSource>()) {
        src->Stop();   // fadeOutSeconds>0 时先淡出、到期再停（无爆音，§28.3.2 裁定 12）
        NovaEngine::Logger::info("NeonGameManager: 已请求 BGM 淡出停止");
    }
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonGameManager)
