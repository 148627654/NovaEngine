// ===== V1.0-3 CharacterController（§27.2）：物理行为实现（cR 波 2）=====
//
// 选型裁定：封装 Jolt CharacterVirtual（楼梯步进/坡度限制/地面检测库内现成）。
// 分工裁定（cR 域内）：全部 Jolt 类型封装在 PhysicsWorld.cpp（pimpl 内），本文件零 Jolt——
// CharacterController 的公开 API 经 friend 桥委托 PhysicsWorld：
//   move()      → PhysicsWorld::characterMove（位移缓冲，固定步消费）
//   velocity()  → PhysicsWorld::characterVelocity（Play 由 CharacterVirtual 驱动；Edit 返回 m_velocity）
//   isGrounded  → PhysicsWorld::characterGrounded（Edit no-op → false）
//   生命周期    → onAwake/析构 registerCharacter/unregisterCharacter
// 固定步进集成：PhysicsWorld::step 的每个固定步（1/60s）调用角色 ExtendedUpdate
//（30/60/144 FPS 一致：累加器与刚体共用，见 PhysicsWorld.cpp）。
// 与 Rigidbody 互斥：同 GO 挂两者 → onAwake 与 PhysicsWorld 同步期各 warn 一次（去重）+
// CharacterController 优先（该 GO 的 Rigidbody 不建 body）。
// 序列化键名（§27.2，骨架日已锁定）：radius/height/stepHeight/slopeLimitDeg/mass/gravityScale。

#include <NovaEngine/Scene/CharacterController.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Physics/PhysicsWorld.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/Scene.h>

namespace NovaEngine {

namespace {

// Edit 态 physics() 可能为 null（组件窗口期）；封装为统一 no-op 路径
PhysicsWorld* physicsOf(const CharacterController* cc) {
    const GameObject* go = cc->gameObject();
    return (go != nullptr && go->scene() != nullptr) ? go->scene()->physics() : nullptr;
}

}  // namespace

CharacterController::CharacterController(GameObject* owner) : Component(owner) {}

CharacterController::~CharacterController() {
    if (PhysicsWorld* pw = physicsOf(this))
        pw->unregisterCharacter(this);   // Play 中销毁 GO：摘除 CharacterVirtual（防 UAF）
}

bool CharacterController::isGrounded() const {
    if (PhysicsWorld* pw = physicsOf(this))
        return pw->characterGrounded(this);
    return false;   // Edit no-op
}

void CharacterController::move(const glm::vec3& displacement) {
    if (PhysicsWorld* pw = physicsOf(this))
        pw->characterMove(this, displacement);
    // Edit no-op（与物理组件同纪律）
}

glm::vec3 CharacterController::velocity() const {
    glm::vec3 out(0.0f);
    if (PhysicsWorld* pw = physicsOf(this))
        if (pw->characterVelocity(this, out))
            return out;   // Play：物理驱动的真实速度
    return m_velocity;    // Edit 缓冲（角色未创建）
}

void CharacterController::setVelocity(const glm::vec3& v) {
    if (PhysicsWorld* pw = physicsOf(this))
        if (pw->setCharacterVelocity(this, v))
            return;       // Play：下一固定步生效
    m_velocity = v;       // Edit 缓冲（进入 Play 由 syncFromTransforms 重建，缓冲丢弃）
}

void CharacterController::setPosition(const glm::vec3& worldPos) {
    if (PhysicsWorld* pw = physicsOf(this))
        pw->characterSetPosition(this, worldPos);   // Play：物理级传送（Edit no-op）
}

void CharacterController::onAwake() {
    // Rigidbody 互斥 warn（§27.2：同 GO 挂两者 = 启动 warn + CC 优先；PhysicsWorld 同步期
    // 会跳过该 Rigidbody 的 body 创建——本处只负责提示，去重一次）
    if (!m_conflictWarned && gameObject() != nullptr && gameObject()->getComponent<Rigidbody>()) {
        m_conflictWarned = true;
        Logger::warn("[Physics] GO \"{}\" 同时挂载 CharacterController 与 Rigidbody：两者互斥，"
                     "CharacterController 优先（Rigidbody 不参与模拟）",
                     gameObject()->name());
    }
    if (PhysicsWorld* pw = physicsOf(this))
        pw->registerCharacter(this);   // Play 中挂载 = 立即创建角色；Edit 待 syncFromTransforms
}

void CharacterController::onUpdate(float /*dt*/) {
    // 物理步进由 PhysicsWorld::step 驱动（固定步 1/60）；本钩子保持空（脚本层 onUpdate 同帧可见
    // 物理写回后的位置——写回发生在 Scene::update 的 step 段，先于广播，与 Rigidbody 一致）
}

void CharacterController::onSerialize(nlohmann::json& out) const {
    out["radius"] = radius;
    out["height"] = height;
    out["stepHeight"] = stepHeight;
    out["slopeLimitDeg"] = slopeLimitDeg;
    out["mass"] = mass;
    out["gravityScale"] = gravityScale;
}

void CharacterController::onDeserialize(const nlohmann::json& in) {
    radius = in.value("radius", 0.4f);
    height = in.value("height", 1.8f);
    stepHeight = in.value("stepHeight", 0.35f);
    slopeLimitDeg = in.value("slopeLimitDeg", 45.0f);
    mass = in.value("mass", 70.0f);
    gravityScale = in.value("gravityScale", 1.0f);
}

}  // namespace NovaEngine

// 组件静态注册（§27.0：内置组件 25→26；注册名经 ComponentRegistry 裁剪为 "CharacterController"）
NE_REGISTER_COMPONENT(NovaEngine::CharacterController)
