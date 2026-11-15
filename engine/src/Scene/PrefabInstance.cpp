// PrefabInstance.cpp —— 预制体实例记录组件（V0.3-9，波 4）
// 挂实例根对象：场景 JSON 中实例对象照常全量写字段（当前值），本组件只负责
// 「实例对象ID → 源对象ID」溯源映射（override 检测 = 编辑器动态比对源 JSON，不在此存储）。
// 序列化：prefabPath（字符串）+ sourceIds [[instId, srcId], ...]（按 instId 排序输出，
// 保证同一实例状态序列化结果确定——快照比对/文件哈希不因 unordered_map 迭代序抖动）。
#include <NovaEngine/Scene/PrefabInstance.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace NovaEngine {

PrefabInstance::PrefabInstance(GameObject* owner) : Component(owner) {}

void PrefabInstance::onSerialize(nlohmann::json& out) const {
    out["prefabPath"] = prefabPath;
    // 按实例 ID 排序输出（unordered_map 迭代序不定 → 排序保证序列化确定性）
    std::vector<std::pair<uint64_t, uint64_t>> pairs(instanceToSource.begin(),
                                                     instanceToSource.end());
    std::sort(pairs.begin(), pairs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    nlohmann::json ids = nlohmann::json::array();
    for (const auto& [inst, src] : pairs)
        ids.push_back(nlohmann::json::array({inst, src}));
    out["sourceIds"] = std::move(ids);
}

void PrefabInstance::onDeserialize(const nlohmann::json& in) {
    // 只存路径字符串；sourceIds 缺省空——源文件缺失/键畸形不崩溃（override 比对侧兜底）
    prefabPath = in.value("prefabPath", std::string());
    instanceToSource.clear();
    const nlohmann::json& ids = in.value("sourceIds", nlohmann::json::array());
    if (!ids.is_array()) return;
    for (const auto& pair : ids) {
        // 畸形项（非数组/长度不足/非无符号数）跳过，不中断整体读取
        if (!pair.is_array() || pair.size() < 2 || !pair[0].is_number_unsigned() ||
            !pair[1].is_number_unsigned())
            continue;
        instanceToSource[pair[0].get<uint64_t>()] = pair[1].get<uint64_t>();
    }
}

}  // namespace NovaEngine

// 组件静态注册（V0.3 波 0；注册名经 ComponentRegistry 裁剪为 "PrefabInstance"）
NE_REGISTER_COMPONENT(NovaEngine::PrefabInstance)
