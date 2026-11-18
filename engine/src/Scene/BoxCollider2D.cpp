// BoxCollider2D.cpp —— 骨架日桩（V0.5-10）：接口冻结于 §25.8；增强实现归 c8（本文件为 c8 独占）。
// 序列化按契约键名往返（center/size/isTrigger）。
#include <NovaEngine/Scene/BoxCollider2D.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

namespace {

glm::vec2 jsonToVec2(const nlohmann::json& j, const glm::vec2& fallback) {
    if (!j.is_array() || j.size() < 2)
        return fallback;
    try {
        return glm::vec2(j[0].get<float>(), j[1].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

BoxCollider2D::BoxCollider2D(GameObject* owner) : Component(owner) {}

void BoxCollider2D::onSerialize(nlohmann::json& out) const {
    out["center"] = nlohmann::json::array({center.x, center.y});
    out["size"] = nlohmann::json::array({size.x, size.y});
    out["isTrigger"] = isTrigger;
}

void BoxCollider2D::onDeserialize(const nlohmann::json& in) {
    center = jsonToVec2(in.value("center", nlohmann::json()), glm::vec2(0.0f));
    size = jsonToVec2(in.value("size", nlohmann::json()), glm::vec2(1.0f));
    isTrigger = in.value("isTrigger", false);
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "BoxCollider2D"）
NE_REGISTER_COMPONENT(NovaEngine::BoxCollider2D)
