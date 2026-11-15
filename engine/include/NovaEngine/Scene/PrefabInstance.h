#pragma once
#include <string>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>
#include "Component.h"

namespace NovaEngine {

// 预制体实例记录组件（V0.3-9，波 4）：挂实例根对象。
// 场景 JSON 中实例对象照常全量写字段（当前值），本组件只负责「实例对象ID → 源对象ID」溯源映射
// （override 检测 = 编辑器动态比对源 JSON，不在此存储）。
class PrefabInstance : public Component {
public:
    explicit PrefabInstance(GameObject* owner);

    std::string prefabPath;                                  // .novaprefab 相对路径
    std::unordered_map<uint64_t, uint64_t> instanceToSource; // 实例对象ID → 源对象ID

    void onSerialize(nlohmann::json& out) const override;    // prefabPath + sourceIds [[instId, srcId], ...]
    void onDeserialize(const nlohmann::json& in) override;   // 缺省空；只存路径字符串，源缺失不崩溃
};

}  // namespace NovaEngine
