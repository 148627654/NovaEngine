#pragma once
#include <glm/vec2.hpp>
#include <nlohmann/json_fwd.hpp>
#include "Component.h"

namespace NovaEngine {

// 2D AABB 碰撞（V0.5-10 v1）：重叠查询 + 事件回调；v1 无刚体动力学（移动由脚本/Transform 驱动）。
// 独立于 Jolt（不注册进 PhysicsWorld；2D 与 3D 物理共存不互扰）。
class BoxCollider2D : public Component {
public:
    explicit BoxCollider2D(GameObject* owner);
    glm::vec2 center{0.0f}; glm::vec2 size{1.0f};   // 局部（2D 平面 = 世界 XY）
    bool isTrigger = false;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
