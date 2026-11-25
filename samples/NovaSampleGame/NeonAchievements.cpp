#include "NeonAchievements.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/FileUtil.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <ctime>
#include <format>
#include <string_view>
#include <nlohmann/json.hpp>

namespace NovaGame {

NeonAchievements* NeonAchievements::s_instance = nullptr;

NeonAchievements::NeonAchievements(NovaEngine::GameObject* owner) : Behaviour(owner) {}

NeonAchievements* NeonAchievements::instance() { return s_instance; }

bool NeonAchievements::pendingToast() const { return !m_toastQueue.empty(); }

std::string NeonAchievements::consumeToast() {
    if (m_toastQueue.empty())
        return {};
    std::string msg = std::move(m_toastQueue.front());
    m_toastQueue.pop_front();
    return msg;
}

void NeonAchievements::onStart() {
    // 注册单例 + 重置本局状态（场景重载后 onStart 重跑 = 新一局）
    s_instance = this;
    m_unlocked.clear();
    m_stats = Stats{};
    m_ghostBest = -1.0f;
    m_prevGate = false;
    m_prevWon = false;
    m_prevFailed = false;
    m_prevHidden = 0;
    m_prevMaxCombo = 0;
    m_prevFalls = 0;
    m_prevDashRun = 0;
    m_prevCollected = 0;
    m_toastQueue.clear();

    // ===== 读成就存档（§28.3.15.6：缺失/坏 JSON → 空状态 + warn 不崩溃）=====
    std::string text;
    if (NovaEngine::FileUtil::readText("saves/achievements.novaachievements", text)) {
        try {
            const nlohmann::json j = nlohmann::json::parse(text);
            if (j.contains("unlocked") && j["unlocked"].is_object()) {
                for (const auto& [k, v] : j["unlocked"].items())
                    if (v.is_number_integer())
                        m_unlocked[k] = v.get<long long>();
            }
            if (j.contains("stats") && j["stats"].is_object()) {
                const auto readInt = [&j](const char* key, int& out) {
                    if (j["stats"].contains(key) && j["stats"][key].is_number_integer())
                        out = j["stats"][key].get<int>();
                };
                readInt("clear", m_stats.clear);
                readInt("dash", m_stats.dash);
                readInt("fall", m_stats.fall);
                readInt("orb", m_stats.orb);
            }
        } catch (const std::exception& e) {
            NovaEngine::Logger::warn("NeonAchievements: 成就存档解析失败（{}），以空状态开始", e.what());
        }
    }

    // ===== 快照本模式幽灵 best（§28.3.15.6 beat_ghost 判定源；无文件 → -1 不判定）=====
    if (NeonGameManager* mgr = NeonGameManager::instance()) {
        const std::string path = (mgr->mode() == "challenge")
                                     ? "saves/ghost_challenge.novaghost"
                                     : "saves/ghost.novaghost";
        std::string ghostText;
        if (NovaEngine::FileUtil::readText(path, ghostText)) {
            try {
                const nlohmann::json j = nlohmann::json::parse(ghostText);
                if (j.contains("best") && j["best"].is_number())
                    m_ghostBest = j["best"].get<float>();
            } catch (...) { /* 坏文件按无幽灵处理 */ }
        }
        NovaEngine::Logger::info(
            "NeonAchievements: 单例已注册，已解锁 {} 项，stats(clear={} dash={} fall={} orb={})，"
            "幽灵 best {:.1f}（mode={}）",
            m_unlocked.size(), m_stats.clear, m_stats.dash, m_stats.fall, m_stats.orb,
            m_ghostBest, mgr->mode());
    } else {
        NovaEngine::Logger::warn("NeonAchievements: 场景缺少 NeonGameManager，成就轮询不生效");
    }
}

void NeonAchievements::onUpdate(float /*dt*/) {
    // 热重载自愈（§28.3.11）：DLL 重载后旧 s_instance 指针失效，重新认领
    if (s_instance != this)
        s_instance = this;

    NeonGameManager* mgr = NeonGameManager::instance();
    if (!mgr)
        return;

    const bool won = mgr->won();
    const bool failed = mgr->failed();
    const bool gate = mgr->gateActive();
    const int hidden = mgr->hiddenCollected();
    const int maxCombo = mgr->maxCombo();
    const int falls = mgr->fallsRun();
    const int dashRun = mgr->dashRun();
    const int collected = mgr->collected();
    const bool challenge = (mgr->mode() == "challenge");

    // ===== stats 累计（§28.3.15.6：全部经轮询边沿，delta 增量防跳变）=====
    if (collected > m_prevCollected) {
        m_stats.orb += collected - m_prevCollected;
        // orb_200「收集狂」：累计收集 200 颗普通球（跨局累计边沿）
        if (m_stats.orb >= 200)
            unlock("orb_200", "收集狂");
    }
    if (falls > m_prevFalls)
        m_stats.fall += falls - m_prevFalls;
    if (dashRun > m_prevDashRun) {
        m_stats.dash += dashRun - m_prevDashRun;
        // dash_50「冲刺达人」：累计冲刺 50 次（跨局累计边沿）
        if (m_stats.dash >= 50)
            unlock("dash_50", "冲刺达人");
    }

    // ===== 判定表逐条（§28.3.15.6：判定数据源 + 时机逐字照做）=====
    // all_orbs「满载而归」：门激活（单局集满全部能量球）边沿
    if (!m_prevGate && gate)
        unlock("all_orbs", "满载而归");
    // hidden_2「晶体猎人」：单局收集 2 颗隐藏晶体边沿（≥2）
    if (m_prevHidden < 2 && hidden >= 2)
        unlock("hidden_2", "晶体猎人");
    // combo_5「连击大师」：单局连击达到 x5 边沿（≥5）
    if (m_prevMaxCombo < 5 && maxCombo >= 5)
        unlock("combo_5", "连击大师");

    // ===== won 边沿判定组（评级/通关/掉落/幽灵；stats.clear++ 放本组末尾）=====
    if (won && !m_prevWon) {
        // first_clear「初次通关」：通关前累计 0 次（任意模式）
        if (m_stats.clear == 0)
            unlock("first_clear", "初次通关");
        // rank_s「完美评级」：经典模式通关且评级 S
        if (!challenge && std::string_view(mgr->rankName()) == "S")
            unlock("rank_s", "完美评级");
        // challenge_clear「分秒必争」：限时挑战通关
        if (challenge)
            unlock("challenge_clear", "分秒必争");
        // challenge_s「时间大师」：限时挑战评级 S（剩余 ≥40s）
        if (challenge && std::string_view(mgr->rankName()) == "S")
            unlock("challenge_s", "时间大师");
        // deathless「完美跑者」：通关且本局 0 次掉落
        if (falls == 0)
            unlock("deathless", "完美跑者");
        // fall_5「摔跤艺术家」：通关且本局掉落 ≥5 次
        if (falls >= 5)
            unlock("fall_5", "摔跤艺术家");
        // beat_ghost「超越自我」：经典=用时 < ghost best；限时=剩余 > ghost best；无文件不判定
        if (m_ghostBest >= 0.0f) {
            const bool beaten = challenge ? (mgr->remaining() > m_ghostBest)
                                          : (mgr->elapsedTime() < m_ghostBest);
            if (beaten)
                unlock("beat_ghost", "超越自我");
        }
        m_stats.clear++;
    }

    // ===== won/failed 上升沿写盘 stats（§28.3.15.2 裁定 9；中途强退丢未写累计属可接受取舍）=====
    if ((won && !m_prevWon) || (failed && !m_prevFailed))
        writeFile();

    m_prevGate = gate;
    m_prevWon = won;
    m_prevFailed = failed;
    m_prevHidden = hidden;
    m_prevMaxCombo = maxCombo;
    m_prevFalls = falls;
    m_prevDashRun = dashRun;
    m_prevCollected = collected;
}

void NeonAchievements::unlock(const char* id, const char* displayName) {
    if (m_unlocked.contains(id))
        return;   // 已解锁：幂等
    m_unlocked[id] = static_cast<long long>(std::time(nullptr));
    writeFile();   // 解锁时原子写盘（§28.3.15.6）
    if (auto* mgr = NeonGameManager::instance())
        mgr->playSfx("assets/audio/sfx_achieve.wav", 1.0f);
    // toast 队列上限 8，超出丢最旧（§28.3.15.6）
    if (m_toastQueue.size() >= 8) {
        m_toastQueue.pop_front();
        NovaEngine::Logger::warn("NeonAchievements: toast 队列已满，丢弃最旧一条");
    }
    m_toastQueue.push_back(std::format("★ 成就解锁:{}", displayName));
    NovaEngine::Logger::info("NeonAchievements: 成就解锁 [{}] {}（已解锁 {}/12）", id,
                             displayName, m_unlocked.size());
}

void NeonAchievements::writeFile() {
    nlohmann::json j;
    j["version"] = 1;
    nlohmann::json unlocked = nlohmann::json::object();
    for (const auto& [k, v] : m_unlocked)
        unlocked[k] = v;
    j["unlocked"] = std::move(unlocked);
    j["stats"] = {{"clear", m_stats.clear},
                  {"dash", m_stats.dash},
                  {"fall", m_stats.fall},
                  {"orb", m_stats.orb}};
    if (!NovaEngine::FileUtil::writeText("saves/achievements.novaachievements", j.dump()))
        NovaEngine::Logger::warn("NeonAchievements: 成就存档写入失败");
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonAchievements)
