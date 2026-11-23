#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include "PhysicsTypes.h"

namespace NovaEngine {

class Scene;
class Rigidbody;
class Collider;
class CharacterController;
class GameObject;

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

    // ===== V1.0-3 物理增强（§27.2 追加；签名冻结）=====
    // 查询过滤：layerMask 位掩码（bit L = 层 L 参与；与层矩阵无关，Unity layermask 语义）；
    // ignore 非空时跳过该 GO 所属 body（触发器/静态体/刚体均可）
    struct RaycastFilter {
        uint32_t layerMask = 0xFFFFFFFF;   // 全部层参与
        GameObject* ignore = nullptr;      // 命中忽略对象（nullptr = 不过滤）
    };
    // 全命中射线（距离升序；触发器也计入命中）；无命中 → false（outHits 为空）
    bool raycastAll(const glm::vec3& origin, const glm::vec3& dir, float maxDist,
                    const RaycastFilter& filter, std::vector<RaycastHit>& outHits);
    // 形状重叠查询（球/盒；命中去重后返回 GO 列表，含触发器；无命中 → false）
    bool overlapSphere(const glm::vec3& center, float radius, const RaycastFilter& filter,
                       std::vector<GameObject*>& outObjects);
    bool overlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
                    const RaycastFilter& filter, std::vector<GameObject*>& outObjects);

    // ===== 碰撞层（矩阵对称；配置 = 场景级 environment.physics 段）=====
    static constexpr int kMaxLayers = 16;
    void setLayerName(int layer, const std::string& name);   // 写回 scene.physicsLayerNames（权威存储）
    const std::string& layerName(int layer);                 // 越界 → 空串引用（静态空串）
    void setCollisionEnabled(int a, int b, bool enabled);    // 对称写：matrix[a][b] == matrix[b][a]
    bool collisionEnabled(int a, int b) const;               // 越界 → false

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

    // V0.5-8 地形高度场（c6）：命名空间级自由函数桥接（文件尾声明），经
    // terrain→GameObject→Scene→PhysicsWorld 定位后调用以下私有入口
    void registerTerrainHeightfieldInternal(class Terrain* terrain);
    void unregisterTerrainHeightfieldInternal(class Terrain* terrain);
    friend void registerTerrainHeightfield(class Terrain* terrain);      // 自由函数（见文件尾声明）
    friend void unregisterTerrainHeightfield(class Terrain* terrain);

    // ===== V1.0-3 CharacterController 宿主桥（cR 私有；CharacterController.cpp 经 friend 调用）=====
    // Jolt CharacterVirtual 全部由 PhysicsWorld::Impl 持有（Scene 组件侧零 Jolt）：
    // 注册/注销（onAwake/析构）、move/velocity 缓冲（脚本调用）、isGrounded 查询。
    // 未 synced（Edit 态）或角色未创建 → 安全 no-op / 默认值（Edit no-op 纪律）。
    friend class CharacterController;
    void registerCharacter(CharacterController* cc);   // onAwake；Play 中挂载 = 立即创建角色
    void unregisterCharacter(CharacterController* cc);
    void characterMove(CharacterController* cc, const glm::vec3& displacement);   // move() 缓冲，固定步消费
    bool characterVelocity(const CharacterController* cc, glm::vec3& out) const;  // false = 角色未创建（Edit）
    bool setCharacterVelocity(CharacterController* cc, const glm::vec3& v);       // false = 角色未创建（Edit）
    bool characterGrounded(const CharacterController* cc) const;                  // false = 未创建/空中
};

// ===== V0.5-8 地形高度场（c6，§25.6 追加）=====
// 创建/重建 Jolt HeightFieldShape 静态体（Terrain::onAwake / rebuildDirtyRegion /
// 析构调用；重建路径 = 销毁旧体 + 全量新建——HeightFieldShape 不可原地修改）。
// v1 限制：HeightField 为世界轴对齐 XZ 网格（Jolt HeightFieldShape 不支持旋转烘焙，
// 地形 GO 的旋转/非均匀缩放不参与碰撞——旋转地形请改用 MeshCollider）。
void registerTerrainHeightfield(class Terrain* terrain);
void unregisterTerrainHeightfield(class Terrain* terrain);

}  // namespace NovaEngine
