// Rigidbody.cpp —— 刚体组件（V0.3-1，§14.1）
//
// 注册规则：onAwake 注册 body（PhysicsWorld::registerRigidbody），析构注销；
// body 的实际创建延迟到「存在有效 Collider」时（无 Collider 的 Rigidbody 不建 body，
// addForce/setVelocity 等安全降级为 no-op）。动态刚体直改 Transform 会被物理覆盖
// （文档化限制），驱动方式 = addForce/addImpulse/setVelocity。

#include <NovaEngine/Scene/Rigidbody.h>

#include <NovaEngine/Physics/PhysicsWorld.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

namespace {

PhysicsWorld* physicsOf(const Rigidbody* rb) {
    const GameObject* go = rb->gameObject();
    return (go != nullptr && go->scene() != nullptr) ? go->scene()->physics() : nullptr;
}

}  // namespace

Rigidbody::Rigidbody(GameObject* owner) : Component(owner) {}

Rigidbody::~Rigidbody() {
    // 注销 body（body 销毁后其 collider 由 PhysicsWorld 重新裁决宿主/static）
    if (PhysicsWorld* pw = physicsOf(this))
        pw->unregisterRigidbody(this);
}

void Rigidbody::onAwake() {
    if (PhysicsWorld* pw = physicsOf(this))
        pw->registerRigidbody(this);
}

void Rigidbody::addForce(const glm::vec3& force) {
    if (PhysicsWorld* pw = physicsOf(this))
        pw->addForce(this, force);
}

void Rigidbody::addImpulse(const glm::vec3& impulse) {
    if (PhysicsWorld* pw = physicsOf(this))
        pw->addImpulse(this, impulse);
}

glm::vec3 Rigidbody::velocity() const {
    if (PhysicsWorld* pw = physicsOf(this))
        return pw->velocity(this);
    return glm::vec3(0.0f);
}

void Rigidbody::setVelocity(const glm::vec3& v) {
    if (PhysicsWorld* pw = physicsOf(this))
        pw->setVelocity(this, v);
}

void Rigidbody::onSerialize(nlohmann::json& out) const {
    out["mass"] = mass;
    out["linearDamping"] = linearDamping;
    out["angularDamping"] = angularDamping;
    out["useGravity"] = useGravity;
    out["isKinematic"] = isKinematic;
}

void Rigidbody::onDeserialize(const nlohmann::json& in) {
    mass = in.value("mass", 1.0f);
    linearDamping = in.value("linearDamping", 0.0f);
    angularDamping = in.value("angularDamping", 0.05f);
    useGravity = in.value("useGravity", true);
    isKinematic = in.value("isKinematic", false);
}

}  // namespace NovaEngine

// 组件静态注册（V0.3 波 0；注册名经 ComponentRegistry 裁剪为 "Rigidbody"）
NE_REGISTER_COMPONENT(NovaEngine::Rigidbody)
