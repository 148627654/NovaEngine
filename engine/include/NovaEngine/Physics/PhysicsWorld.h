#pragma once
#include <functional>
#include <memory>
#include <glm/vec3.hpp>
#include "PhysicsTypes.h"

namespace NovaEngine {

class Scene;
class Rigidbody;
class Collider;

// Scene 级物理服务（V0.3-1，Jolt 后端）：固定步进 60Hz；公共头无 Jolt（pimpl Impl）
class PhysicsWorld {
public:
    static constexpr float kFixedStep = 1.0f / 60.0f;

    explicit PhysicsWorld(Scene& scene);          // Scene 构造时创建
    ~PhysicsWorld();                              // 定义在 .cpp（Impl 完整类型）
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void step(float dt);                          // 固定步进累加器，每帧最多 3 步防死亡螺旋；由 Scene::update 调用
    void syncFromTransforms();                    // 进入 Play 时：全部 body 重置到当前 Transform（由 Scene::start 调用）
    // 批量销毁旗标（2026-11-15 物理验收修复）：~Scene 与 deserialize 清空前调用——
    // 组件析构期的 unregister 不再「重建/重挂」（避免对已析构 GO 的 UAF），
    // 残留 body 由 teardownAll 统一销毁；syncFromTransforms 复位（下一次 Play 全量重建）。
    void beginTeardown();
    bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                 float maxDistance, RaycastHit& outHit);
    void setGravity(const glm::vec3& g);
    glm::vec3 gravity() const;

    // 调试线框：Jolt DebugRenderer 提取线段 → 编辑器 GL_LINES 消费（每对 (a,b) 一条）
    void debugDraw(const std::function<void(const glm::vec3& a, const glm::vec3& b)>& line);

    // ===== V0.3-1 组件注册与刚体接口（Rigidbody/Collider 的 onAwake/析构调用）=====
    // 注册规则：仅 Collider → static body；Rigidbody → dynamic/kinematic；
    // Collider 可挂同 GO 或子 GO 附加形状（详见 PhysicsWorld.cpp 头部裁定注释）。
    void registerRigidbody(Rigidbody* rb);
    void unregisterRigidbody(Rigidbody* rb);
    void registerCollider(Collider* collider);
    void unregisterCollider(Collider* collider);
    // 刚体运行期接口（Play 中脚本调用；body 未创建时安全降级为 no-op）
    void addForce(Rigidbody* rb, const glm::vec3& force);
    void addImpulse(Rigidbody* rb, const glm::vec3& impulse);
    glm::vec3 velocity(const Rigidbody* rb) const;
    void setVelocity(Rigidbody* rb, const glm::vec3& v);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace NovaEngine
