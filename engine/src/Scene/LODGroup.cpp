// LODGroup.cpp —— 骨架日桩（V0.5-3）：接口冻结于 §25.1；选档实现归 c1（本文件为 c1 独占）。
// 冻结钩子 resolveMeshFor 当前恒返回 nullptr → 调用方回退 mr->mesh = 旧路径零变化。
#include <NovaEngine/Scene/LODGroup.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

LODGroup::LODGroup(GameObject* owner) : Component(owner) {}

std::shared_ptr<Mesh> LODGroup::resolveMeshFor(GameObject* go, const glm::vec3& cameraPos) {
    (void)go;
    (void)cameraPos;
    return nullptr;   // 骨架日空实现：无可用档 → 调用方回退 mr->mesh（c1 实现选档后替换）
}

void LODGroup::onSerialize(nlohmann::json& out) const {
    out["enabled"] = enabled;
    out["crossFade"] = crossFade;
    nlohmann::json arr = nlohmann::json::array();
    for (const LODLevel& level : lods) {
        nlohmann::json j;
        j["mesh"] = level.meshPath;
        j["distance"] = level.distance;
        arr.push_back(std::move(j));
    }
    out["lods"] = std::move(arr);
}

void LODGroup::onDeserialize(const nlohmann::json& in) {
    enabled = in.value("enabled", true);
    crossFade = in.value("crossFade", false);
    lods.clear();
    if (in.contains("lods") && in["lods"].is_array()) {
        for (const auto& j : in["lods"]) {
            if (!j.is_object()) continue;
            LODLevel level;
            level.meshPath = j.value("mesh", std::string());
            level.distance = j.value("distance", 0.0f);
            lods.push_back(std::move(level));
        }
    }
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名经 ComponentRegistry 裁剪为 "LODGroup"）
NE_REGISTER_COMPONENT(NovaEngine::LODGroup)
