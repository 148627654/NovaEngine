#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <glm/vec3.hpp>
#include "Component.h"

namespace NovaEngine {

// 碰撞器基类（V0.3-1）：仅 Collider 无 Rigidbody → static body（地面类）；
// Rigidbody 可无 Collider；Collider 可挂同 GO 或子 GO
class Collider : public Component {
public:
    explicit Collider(GameObject* owner);
    ~Collider() override;   // 注销 shape（定义在 Collider.cpp；派生类共用此析构）

    bool isTrigger = false;                       // sensor：不碰撞求解，只发触发事件（事件系统 TriggerEvent）
    glm::vec3 center{0.0f};                       // 局部偏移

    void onAwake() override;   // 注册 shape（宿主刚体/static body 由 PhysicsWorld 裁决）
    // 公共字段由基类 onSerialize 写，形状参数由子类写
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

class BoxCollider : public Collider {
public:
    explicit BoxCollider(GameObject* owner);
    glm::vec3 halfExtents{0.5f};
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

class SphereCollider : public Collider {
public:
    explicit SphereCollider(GameObject* owner);
    float radius = 0.5f;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

class CapsuleCollider : public Collider {
public:
    explicit CapsuleCollider(GameObject* owner);
    float radius = 0.5f, height = 2.0f;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

class MeshCollider : public Collider {
public:
    explicit MeshCollider(GameObject* owner);
    std::string meshPath;                         // 静态三角网格（经 ModelLoader/BinaryCache 取 MeshData）
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
