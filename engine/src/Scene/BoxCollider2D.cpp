// BoxCollider2D.cpp —— 2D AABB 碰撞体组件（V0.5-10，c8 定稿；接口冻结于 §25.8）
// 序列化按契约键名往返（center/size/isTrigger）。纯数据组件：重叠检测与事件投递在
// Physics2D::step（v1 无刚体动力学，移动由脚本/Transform 驱动；独立于 Jolt）。
#include <NovaEngine/Scene/BoxCollider2D.h>

#include <NovaEngine/Core/Logger.h>
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
    if (size.x <= 0.0f || size.y <= 0.0f) {   // 非法尺寸回退默认（半宽≤0 无有效 AABB）
        Logger::warn("BoxCollider2D 反序列化：size ({}, {}) 非法，回退 (1, 1)", size.x, size.y);
        size = glm::vec2(1.0f);
    }
    isTrigger = in.value("isTrigger", false);
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "BoxCollider2D"）
NE_REGISTER_COMPONENT(NovaEngine::BoxCollider2D)
