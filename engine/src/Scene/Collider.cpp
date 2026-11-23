// Collider.cpp —— 碰撞器组件族（V0.3-1，§14.1）
//
// 注册规则（详见 PhysicsWorld.cpp 头部裁定）：仅 Collider（无 Rigidbody）→ static body；
// 同 GO / 子 GO 的 Collider 附加到宿主刚体的 shape；isTrigger = sensor（body 级）。
// 基类 Collider 自身不注册（无独立实例语义），四个具体类注册进 ComponentRegistry——
// NE_REGISTER_COMPONENT 宏的注册结构名在同一 TU 内会冲突，故本文件改用手写静态注册
// （语义与宏完全一致：幂等 registerBuiltin）。

#include <NovaEngine/Scene/Collider.h>

#include <NovaEngine/Physics/PhysicsWorld.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <string>

namespace NovaEngine {

namespace {

PhysicsWorld* physicsOf(const Collider* c) {
    const GameObject* go = c->gameObject();
    return (go != nullptr && go->scene() != nullptr) ? go->scene()->physics() : nullptr;
}

glm::vec3 jsonToVec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    try {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

// ==================== 基类 ====================

Collider::Collider(GameObject* owner) : Component(owner) {}

Collider::~Collider() {
    // 注销 shape（宿主 body 形状重建；空 static body 自动销毁）
    if (PhysicsWorld* pw = physicsOf(this))
        pw->unregisterCollider(this);
}

void Collider::onAwake() {
    if (PhysicsWorld* pw = physicsOf(this))
        pw->registerCollider(this);
}

void Collider::onSerialize(nlohmann::json& out) const {
    out["isTrigger"] = isTrigger;
    out["center"] = nlohmann::json::array({center.x, center.y, center.z});
    // V1.0-3（§27.2）：layer/friction/restitution 纯增量键，version 4 不变
    out["layer"] = layer;
    out["friction"] = friction;
    out["restitution"] = restitution;
}

void Collider::onDeserialize(const nlohmann::json& in) {
    isTrigger = in.value("isTrigger", false);
    center = jsonToVec3(in.value("center", nlohmann::json()), glm::vec3(0.0f));
    // V1.0-3（§27.2）：缺省兜底（旧文件零变化）；layer 钳制 0..15
    layer = std::clamp(in.value("layer", 0), 0, 15);
    friction = std::clamp(in.value("friction", 0.5f), 0.0f, 10.0f);
    restitution = std::clamp(in.value("restitution", 0.0f), 0.0f, 1.0f);
}

// ==================== BoxCollider ====================

BoxCollider::BoxCollider(GameObject* owner) : Collider(owner) {}

void BoxCollider::onSerialize(nlohmann::json& out) const {
    Collider::onSerialize(out);
    out["halfExtents"] = nlohmann::json::array({halfExtents.x, halfExtents.y, halfExtents.z});
}

void BoxCollider::onDeserialize(const nlohmann::json& in) {
    Collider::onDeserialize(in);
    halfExtents = jsonToVec3(in.value("halfExtents", nlohmann::json()), glm::vec3(0.5f));
}

// ==================== SphereCollider ====================

SphereCollider::SphereCollider(GameObject* owner) : Collider(owner) {}

void SphereCollider::onSerialize(nlohmann::json& out) const {
    Collider::onSerialize(out);
    out["radius"] = radius;
}

void SphereCollider::onDeserialize(const nlohmann::json& in) {
    Collider::onDeserialize(in);
    radius = in.value("radius", 0.5f);
}

// ==================== CapsuleCollider ====================

CapsuleCollider::CapsuleCollider(GameObject* owner) : Collider(owner) {}

void CapsuleCollider::onSerialize(nlohmann::json& out) const {
    Collider::onSerialize(out);
    out["radius"] = radius;
    out["height"] = height;   // 总高（含两端半球，Unity 语义）
}

void CapsuleCollider::onDeserialize(const nlohmann::json& in) {
    Collider::onDeserialize(in);
    radius = in.value("radius", 0.5f);
    height = in.value("height", 2.0f);
}

// ==================== MeshCollider ====================

MeshCollider::MeshCollider(GameObject* owner) : Collider(owner) {}

void MeshCollider::onSerialize(nlohmann::json& out) const {
    Collider::onSerialize(out);
    out["meshPath"] = meshPath;
}

void MeshCollider::onDeserialize(const nlohmann::json& in) {
    Collider::onDeserialize(in);
    meshPath = in.value("meshPath", std::string());
}

}  // namespace NovaEngine

namespace {

// 手写静态注册（NE_REGISTER_COMPONENT 宏的注册结构名同 TU 冲突，见文件头注释）
struct ColliderComponentRegistrars {
    ColliderComponentRegistrars() {
        using namespace NovaEngine;
        ComponentRegistry::registerBuiltin(
            "BoxCollider", Component::componentTypeId<BoxCollider>(),
            [](GameObject* go) { return std::make_unique<BoxCollider>(go); });
        ComponentRegistry::registerBuiltin(
            "SphereCollider", Component::componentTypeId<SphereCollider>(),
            [](GameObject* go) { return std::make_unique<SphereCollider>(go); });
        ComponentRegistry::registerBuiltin(
            "CapsuleCollider", Component::componentTypeId<CapsuleCollider>(),
            [](GameObject* go) { return std::make_unique<CapsuleCollider>(go); });
        ComponentRegistry::registerBuiltin(
            "MeshCollider", Component::componentTypeId<MeshCollider>(),
            [](GameObject* go) { return std::make_unique<MeshCollider>(go); });
    }
};
const ColliderComponentRegistrars s_colliderComponentRegistrars;

}  // namespace
