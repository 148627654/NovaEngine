#include "NeonGameManager.h"

#include <NovaEngine/Audio/AudioClip.h>
#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/Time.h>
#include <NovaEngine/SaveGame/SaveGame.h>
#include <NovaEngine/Scene/AudioSource.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SceneManager.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

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

    // 读关卡参数（§28.3.10：缺失/坏 JSON → 默认 index=1、orbTotal=10）
    const std::string raw = NovaEngine::SceneManager::getSceneParam("level");
    if (!raw.empty()) {
        try {
            const nlohmann::json j = nlohmann::json::parse(raw);
            if (j.contains("index") && j["index"].is_number_integer())
                m_levelIndex = j["index"].get<int>();
            if (j.contains("orbTotal") && j["orbTotal"].is_number_integer())
                m_orbTotal = j["orbTotal"].get<int>();
        } catch (const std::exception& e) {
            NovaEngine::Logger::warn("NeonGameManager: level 参数解析失败({})，回退默认 index=1、orbTotal=10",
                                     e.what());
            m_levelIndex = 1;
            m_orbTotal = 10;
        }
    }
    NovaEngine::Logger::info("NeonGameManager: 单例已注册，关卡 index={} orbTotal={}（计时起点 {:.2f}s）",
                             m_levelIndex, m_orbTotal, m_startTime);
}

void NeonGameManager::onUpdate(float /*dt*/) {
    // 热重载自愈（§28.3.11）：DLL 重载后旧 s_instance 指针失效，重新认领
    if (s_instance != this) {
        s_instance = this;
        NovaEngine::Logger::info("NeonGameManager: 检测到单例失效，已重新注册（热重载自愈）");
    }
}

void NeonGameManager::notifyCollected() {
    ++m_collected;
    NovaEngine::Logger::info("NeonGameManager: 收集能量球 {}/{}", m_collected, m_orbTotal);
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

    m_won = true;
    m_endTime = NovaEngine::Time::totalTime();
    const float elapsed = m_endTime - m_startTime;

    // 最佳纪录 = min(旧最佳, 本次)（§28.3.12：写档前先读槽 0 只读元数据）
    float oldBest = elapsed;
    for (const auto& slot : NovaEngine::SaveGame::listSlots()) {
        if (slot.slot == 0 && slot.valid) {
            oldBest = slot.playTimeSeconds;
            break;
        }
    }
    m_bestTime = std::min(oldBest, elapsed);

    // 通关写档 slot 0（§28.3.12：原子写 saves/slot0.novasave）
    if (NovaEngine::Scene* scene = gameObject()->scene())
        NovaEngine::SaveGame::save(*scene, 0, "assets/scenes/NeonLevel1.nova.json", elapsed);

    playSfx("assets/audio/sfx_win.wav", 1.0f);
    stopBgm();   // 胜利画面：BGM 按 fadeOutSeconds 淡出
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
