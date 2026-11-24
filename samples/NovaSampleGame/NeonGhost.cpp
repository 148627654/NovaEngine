#include "NeonGhost.h"
#include "NeonGameManager.h"

#include <NovaEngine/Core/FileUtil.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaGame {

NeonGhost::NeonGhost(NovaEngine::GameObject* owner) : Behaviour(owner) {}

void NeonGhost::onStart() {
    // 读取最佳纪录轨迹；无文件 → 自行隐藏（本局无幽灵可赛）。
    // 注：GO 在场景里必须 active=true——引擎不会给未激活对象跑 onStart，
    // 隐藏动作只能由脚本在 onStart 里执行（V1.2 场景约定）。
    std::string text;
    if (!NovaEngine::FileUtil::readText("saves/ghost.novaghost", text)) {
        NovaEngine::Logger::info("NeonGhost: 无幽灵回放文件，本局隐藏");
        gameObject()->setActiveSelf(false);
        return;
    }
    try {
        const nlohmann::json j = nlohmann::json::parse(text);
        for (const auto& s : j.at("samples")) {
            if (s.is_array() && s.size() >= 4 && s[0].is_number())
                m_samples.push_back({s[0].get<float>(),
                                     glm::vec3(s[1].get<float>(), s[2].get<float>(),
                                               s[3].get<float>())});
        }
    } catch (const std::exception& e) {
        NovaEngine::Logger::warn("NeonGhost: 幽灵文件解析失败（{}），本局隐藏", e.what());
        m_samples.clear();
        return;
    }
    if (m_samples.empty())
        return;
    m_active = true;
    gameObject()->setActiveSelf(true);
    NovaEngine::Logger::info("NeonGhost: 幽灵已加载（{} 个采样，最佳 {:.1f}s）", m_samples.size(),
                             m_samples.back().t);
}

void NeonGhost::onUpdate(float /*dt*/) {
    if (!m_active || !enabled)
        return;
    NeonGameManager* mgr = NeonGameManager::instance();
    if (!mgr || mgr->won())
        return;   // 结算后幽灵停住
    const float t = mgr->elapsedTime();
    // 二分定位时间区间 [i, i+1]，线性插值
    if (t <= m_samples.front().t) {
        gameObject()->transform()->setLocalPosition(m_samples.front().pos);
        return;
    }
    if (t >= m_samples.back().t) {
        gameObject()->transform()->setLocalPosition(m_samples.back().pos);
        return;
    }
    size_t lo = 0, hi = m_samples.size() - 1;
    while (lo + 1 < hi) {
        const size_t mid = (lo + hi) / 2;
        if (m_samples[mid].t <= t)
            lo = mid;
        else
            hi = mid;
    }
    const Sample& a = m_samples[lo];
    const Sample& b = m_samples[hi];
    const float f = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 0.0f;
    gameObject()->transform()->setLocalPosition(glm::mix(a.pos, b.pos, f));
}

}  // namespace NovaGame

NE_REGISTER_SCRIPT(NovaGame::NeonGhost)
