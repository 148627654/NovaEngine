// PhysicsWorld.cpp —— Jolt 物理后端（V0.3-1，§14.1）
//
// 注册规则（本文件裁定，任务书允许自由裁定 + 注释说明）：
// - 一个 body = 一个带 Rigidbody 的 GO（dynamic / kinematic），或一个仅有 Collider 的 GO（static）。
// - Collider 与 Rigidbody 同 GO → 直接作为该 body 的 shape（局部偏移 = center）。
// - Collider 挂在 Rigidbody GO 的子 GO（且中间无其它 Rigidbody）→ 作为该刚体的附加 shape，
//   局部偏移 = 注册/sync 时刻子 GO 相对刚体 GO 的局部 TRS 烘焙（运行期移动子 Collider 需重挂）。
// - 多 shape body 经 JPH::CompoundShape 合成（静态合成形状，动态刚体质量属性自动按子 shape 计算）；
//   单 shape 直接用原始 shape（有 center 偏移时包一层 RotatedTranslatedShape）。
// - 传感器（isTrigger）为 body 级：body 的全部 Collider 均为 isTrigger 时该 body 为 sensor
//   （JPH::BodyCreationSettings::mIsSensor），sensor 一律 Kinematic（由 Transform 驱动）。
//   文档化限制：混合 trigger/实体 collider 的 body 按实体处理；每 body 对只发一次 Enter/Exit
//   （按子 shape 接触计数）。
// - MeshCollider 仅支持 static/kinematic（Jolt MeshShape 限制）；dynamic body 带 MeshCollider
//   时 warn 并跳过该 collider。
// - 缩放烘焙：Jolt shape 无缩放，GO 的局部缩放（含非均匀）在创建 shape 时烘焙进形状参数。
// - Edit 态不 step（Scene::update 只在 Play 调用）；Scene::start() → syncFromTransforms 全量重建，
//   onAwake/析构走增量注册路径（两者最终一致）。
// - Trigger Enter/Exit 由 ContactListener 在工作线程收集（仅记录，互斥保护），step 帧末主线程
//   统一 Events::post(TriggerEvent{...})（延迟事件，Scene::update 的 flushDeferred 投递）。
//
// 帧内顺序（§15）：Scene::update → flushDestroyQueue → step（本文件）→ onUpdate 广播
// → 协程 advance(false) → Events::flushDeferred（触发器事件在此投递）→ 协程 advance(true)。

#include <NovaEngine/Physics/PhysicsWorld.h>

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/ModelLoader.h>
#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/CharacterController.h>   // V1.0-3：角色宿主桥
#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Terrain/Terrain.h>   // V0.5-8：地形高度场静态体

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>   // V1.0-3：楼梯步进/坡度限制/地面检测
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>   // V1.0-3：CollideShapeSettings/CollideShapeResult
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>   // V1.0-3：AllHitCollisionCollector
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Renderer/DebugRenderer.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NovaEngine {

namespace {

// ===== 对象层设计（V1.0-3 §27.2：16 碰撞层 × 2 运动类 = 32 对象层）=====
// objectLayer = (moving ? 16 : 0) + layer；broadphase 仍 2 层（静/动）。
// 层矩阵（对称 16×16）→ MatrixPairFilter（窄相成对判定）+ MatrixVsBpFilter（宽相逐层掩码）。
// 缺省全通矩阵 = 旧行为（任意层任意运动类互撞）；static-static 配对本不生成（静体不动）。
namespace Layers {
constexpr uint32_t kNumObjectLayers = 32;    // 16 碰撞层 × (static / moving)
constexpr uint32_t kNumBroadPhaseLayers = 2;
constexpr JPH::BroadPhaseLayer kBpNonMoving(0);
constexpr JPH::BroadPhaseLayer kBpMoving(1);
constexpr JPH::ObjectLayer objectLayerOf(int layer, bool moving) {
    return JPH::ObjectLayer(uint32_t(layer) + (moving ? 16u : 0u));
}
constexpr int layerOf(JPH::ObjectLayer ol) {
    const uint32_t v = uint32_t(ol);
    return int(v < 16u ? v : v - 16u);
}
}  // namespace Layers

// ===== V1.0-3 层矩阵过滤器 =====
// 矩阵/掩码为「指针引用」（不复制）：绑定到 PhysicsWorld::Impl 的稳定 std::array 成员，
// 构造体首行设置、矩阵变化时原位改写 + refreshBroadphasePairs 重建宽相配对——
// PhysicsSystem 持有的过滤器引用地址不变（Jolt 缓存对象层表指针，重建对象 = 悬垂）。
class MatrixPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override {
        return matrix[Layers::layerOf(inLayer1) * 16 + Layers::layerOf(inLayer2)];
    }
    const bool* matrix = nullptr;   // 16×16 行主序（对称）
};

class MatrixVsBpFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer, JPH::BroadPhaseLayer) const override {
        return layerMasks[Layers::layerOf(inLayer)] != 0;
    }
    const uint32_t* layerMasks = nullptr;   // 16 项：每层可碰撞层位掩码
};

// 查询用：layerMask 位掩码（Unity layermask 语义，与层矩阵无关）
class MaskObjectLayerFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer) const override {
        return ((mask >> Layers::layerOf(inLayer)) & 1u) != 0;
    }
    uint32_t mask = 0xFFFFFFFF;
};

// 查询用：ignore GO（body 级忽略——多 collider body 整体跳过）
class IgnoreBodyFilter final : public JPH::BodyFilter {
public:
    bool ShouldCollide(const JPH::BodyID& inBodyID) const override {
        if (ignore == nullptr)
            return true;
        const auto it = goByBodyId->find(inBodyID.GetIndexAndSequenceNumber());
        return it == goByBodyId->end() || it->second != ignore;
    }
    bool ShouldCollideLocked(const JPH::Body& inBody) const override {
        return ignore == nullptr || inBody.GetUserData() != reinterpret_cast<uint64_t>(ignore);
    }
    const std::unordered_map<uint32_t, GameObject*>* goByBodyId = nullptr;
    GameObject* ignore = nullptr;
};

// CharacterVirtual 用：矩阵第 ccLayer 行（该层与各层的开关）
class MatrixRowObjectFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer) const override {
        return row[Layers::layerOf(inLayer)];
    }
    const bool* row = nullptr;
};

// ===== Jolt 进程级初始化（RegisterTypes 进程全局；多个 Scene/PhysicsWorld 引用计数）=====
std::atomic<int> s_joltRefCount{0};
std::mutex s_joltInitMutex;

void ensureJoltInitialized() {
    std::lock_guard lock(s_joltInitMutex);
    if (s_joltRefCount.fetch_add(1) == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
#ifdef JPH_ENABLE_ASSERTS
        // 诊断：Jolt 断言转发到引擎日志（不回断点，可一次收集全部断言；
        // 默认行为是 OutputDebugString + __debugbreak → 无人值守直接退出码 3）
        JPH::AssertFailed = [](const char* expr, const char* msg, const char* file, JPH::uint line) {
            Logger::info("[JoltAssert] {}:{} — {} ({})", file, line, expr, msg ? msg : "");
            return false;
        };
#endif
    }
}

void releaseJolt() {
    std::lock_guard lock(s_joltInitMutex);
    if (s_joltRefCount.fetch_sub(1) == 1) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

// DevOps 修复（启动崩溃）：RAII 守卫配对 ensure/release，绑定成员构造/析构顺序。
// 根因：ensureJoltInitialized 原在 Impl 构造体首行，但 pairFilter 等 Jolt 成员在
// 初始化列表阶段先构造，其内部 Array::resize 经 STLAllocator 调用 JPH::Allocate——
// 分配器未注册（空函数指针）→ 空调用崩溃。守卫声明在 Impl 成员区最前：最先构造
// （先注册分配器），最后析构（teardown 完成后再注销，见 Impl 成员区注释）。
struct JoltInitGuard {
    JoltInitGuard() { ensureJoltInitialized(); }
    ~JoltInitGuard() { releaseJolt(); }
};

// ===== glm ↔ Jolt 转换 =====
inline JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::RVec3 toJoltR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
inline JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
// 注：单精度下 RVec3 == Vec3，只保留一个重载（双精度构建需另议）
inline glm::vec3 toGlm(const JPH::RVec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline glm::quat toGlm(const JPH::Quat& q) { return glm::quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW()); }

// body 对 → 64 位 key（BodyID 为 32 位 index+sequence；排序后组合，与顺序无关）
inline uint64_t pairKey(JPH::BodyID a, JPH::BodyID b) {
    const uint32_t ia = a.GetIndexAndSequenceNumber();
    const uint32_t ib = b.GetIndexAndSequenceNumber();
    return (uint64_t(std::min(ia, ib)) << 32) | uint64_t(std::max(ia, ib));
}

// ===== 触发器接触监听（Jolt 工作线程回调 → 仅记录；主线程帧末消费）=====
struct SensorContactEvent {
    JPH::BodyID sensorBody;
    JPH::BodyID otherBody;
    bool entered = false;
};

struct SensorPairEntry {
    JPH::BodyID sensor;
    int count = 0;
};

class ContactListenerImpl final : public JPH::ContactListener {
public:
    JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&, JPH::RVec3Arg,
                                          const JPH::CollideShapeResult&) override {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }
    void OnContactAdded(const JPH::Body& inA, const JPH::Body& inB,
                        const JPH::ContactManifold&, JPH::ContactSettings&) override {
        if (!inA.IsSensor() && !inB.IsSensor())
            return;
        std::lock_guard lock(mutex);
        const uint64_t key = pairKey(inA.GetID(), inB.GetID());
        auto it = sensorPairs.find(key);
        if (it == sensorPairs.end()) {
            // 首次接触 → Enter（每 body 对一次；子 shape 级多触点仅计数）
            sensorPairs[key] = {inA.IsSensor() ? inA.GetID() : inB.GetID(), 1};
            events.push_back({inA.IsSensor() ? inA.GetID() : inB.GetID(),
                              inA.IsSensor() ? inB.GetID() : inA.GetID(), true});
        } else {
            ++it->second.count;
        }
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& inPair) override {
        std::lock_guard lock(mutex);
        const JPH::BodyID b1 = inPair.GetBody1ID();
        const JPH::BodyID b2 = inPair.GetBody2ID();
        const uint64_t key = pairKey(b1, b2);
        auto it = sensorPairs.find(key);
        if (it == sensorPairs.end())
            return;
        if (--it->second.count <= 0) {
            const JPH::BodyID sensor = it->second.sensor;
            const JPH::BodyID other = (b1 == sensor) ? b2 : b1;
            events.push_back({sensor, other, false});
            sensorPairs.erase(it);
        }
    }

    std::mutex mutex;
    std::unordered_map<uint64_t, SensorPairEntry> sensorPairs;  // key → (sensorBody, 触点计数)
    std::vector<SensorContactEvent> events;                     // swap 模式消费
};

// ===== 调试线框收集器（只消费线段；三角/文字忽略）=====
class DebugLineCollector final : public JPH::DebugRenderer {
public:
    explicit DebugLineCollector(const std::function<void(const glm::vec3&, const glm::vec3&)>& line)
        : m_line(line) {}

    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg) override {
        m_line(toGlm(inFrom), toGlm(inTo));
    }
    void DrawTriangle(JPH::RVec3Arg, JPH::RVec3Arg, JPH::RVec3Arg, JPH::ColorArg,
                      JPH::DebugRenderer::ECastShadow) override {}
    Batch CreateTriangleBatch(const Triangle*, int) override { return nullptr; }
    Batch CreateTriangleBatch(const Vertex*, int, const JPH::uint32*, int) override { return nullptr; }
    void DrawGeometry(JPH::RMat44Arg, const JPH::AABox&, float, JPH::ColorArg,
                      const JPH::DebugRenderer::GeometryRef&, JPH::DebugRenderer::ECullMode,
                      JPH::DebugRenderer::ECastShadow, JPH::DebugRenderer::EDrawMode) override {}
    void DrawText3D(JPH::RVec3Arg, const JPH::string_view&, JPH::ColorArg, float) override {}

private:
    std::function<void(const glm::vec3&, const glm::vec3&)> m_line;
};

}  // namespace

// ===== pimpl：全部 Jolt 类型只在本 TU 出现，公共头零 Jolt =====
struct PhysicsWorld::Impl {
    explicit Impl(Scene& scene);
    ~Impl();

    // —— 注册（增量路径；Scene::start → syncFromTransforms 走全量路径）——
    void registerRigidbody(Rigidbody* rb);
    void unregisterRigidbody(Rigidbody* rb);
    void registerCollider(Collider* c);
    void unregisterCollider(Collider* c);

    // —— body 构建 ——
    JPH::BodyID createBodyFor(Rigidbody* rb);            // 无 Collider 时不建 body（返回 Invalid）
    JPH::BodyID rebuildStaticBody(GameObject* go);       // 同 GO 的 collider → static body（全量重建该 body）
    void destroyBody(JPH::BodyID id);
    void destroyStaticBodiesInSubtree(GameObject* root);
    void collectBodyColliders(GameObject* bodyGo, std::vector<Collider*>& out);
    Rigidbody* findHostRigidbody(Collider* c);           // 同 GO → 最近祖先 Rigidbody
    void rehostOrStatic(Collider* c);
    void teardownAll();

    // —— shape 构建 ——
    JPH::Ref<JPH::Shape> buildShape(const std::vector<Collider*>& colliders, GameObject* bodyGo,
                                    bool& outSensor);
    JPH::Ref<JPH::Shape> buildPrimitive(const Collider* c, const glm::vec3& relScale);
    JPH::Ref<JPH::Shape> buildMeshShape(const MeshCollider* mc, const glm::vec3& relScale);

    // —— 地形高度场（V0.5-8，c6）：HeightFieldShape 不可原地修改 → 重建路径 = 销毁 + 全量新建 ——
    void registerTerrainHeightfield(Terrain* t);
    void unregisterTerrainHeightfield(Terrain* t);
    JPH::BodyID buildTerrainBody(Terrain* t);

    // —— 固定步进 ——
    void writeKinematics();   // kinematic/sensor：Transform → body
    void writeBackActive();   // dynamic：body → Transform（父级矩阵反解局部 TRS）
    void flushTriggerEvents();

    // —— V1.0-3 碰撞层矩阵 ——
    void recomputeLayerMasks();                        // collisionMatrix → layerMasks
    void syncFromSceneMatrix();                        // scene.physicsCollisionMatrix → collisionMatrix
    void refreshBroadphasePairs();                     // 矩阵变化后：全部 body 重入宽相（重建配对）

    // —— V1.0-3 CharacterController 宿主（角色全部 Jolt 封装在本文件；Scene 组件侧零 Jolt）——
    JPH::CharacterVirtual* createCharacterFor(CharacterController* cc);
    void destroyCharacter(CharacterController* cc);
    void stepCharacters(float dt);                     // 每固定步：消费 move/velocity 缓冲 + ExtendedUpdate
    void writeBackCharacters();                        // CharacterVirtual → Transform
    bool overlapWithShape(const JPH::Shape* shape, const glm::vec3& center,
                          const PhysicsWorld::RaycastFilter& filter,
                          std::vector<GameObject*>& outObjects);

    // ===== 数据 =====
    // DevOps 修复：守卫必须声明在最前——Jolt 分配器先于一切 Jolt 容器成员注册
    // （pairFilter 构造即分配内存）；析构顺序相反，保证 teardownAll 之后才注销。
    JoltInitGuard joltInitGuard;
    Scene& scene;
    bool synced = false;      // syncFromTransforms 后为 true（Play 态）；step 以此为门
    bool tearingDown = false; // 批量销毁期：注册/注销入口直返（防析构期重建 UAF），syncFromTransforms 复位
    float accumulator = 0.0f;
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};

    // ===== V1.0-3 层矩阵存储（过滤器经指针引用；std::array 地址稳定）=====
    std::array<bool, 256> collisionMatrix{};   // 16×16 行主序（对称；构造体首行全 true 缺省）
    std::array<uint32_t, 16> layerMasks{};     // 每层可碰撞层位掩码（矩阵行压缩）

    // Jolt 核心（成员顺序 = 构造顺序，勿动）
    // V1.0-3：pairFilter/objectVsBpFilter 改为「指针引用」式过滤器（见 MatrixPairFilter 注释）——
    // 不再依赖构造顺序建位图；矩阵变化 = 原位改写 + refreshBroadphasePairs。
    struct ConfiguredLayerInterface : JPH::BroadPhaseLayerInterfaceTable {
        ConfiguredLayerInterface()
            : JPH::BroadPhaseLayerInterfaceTable(Layers::kNumObjectLayers,
                                                 Layers::kNumBroadPhaseLayers) {
            for (int L = 0; L < 16; ++L) {
                MapObjectToBroadPhaseLayer(Layers::objectLayerOf(L, false), Layers::kBpNonMoving);
                MapObjectToBroadPhaseLayer(Layers::objectLayerOf(L, true), Layers::kBpMoving);
            }
        }
    };
    ConfiguredLayerInterface layerInterface;
    MatrixPairFilter pairFilter;
    MatrixVsBpFilter objectVsBpFilter;
    JPH::TempAllocatorImpl tempAllocator{10 * 1024 * 1024};
    JPH::JobSystemThreadPool jobSystem{1024, 8,
                                       std::max(1, int(std::thread::hardware_concurrency()) - 1)};
    JPH::PhysicsSystem system;
    JPH::BodyInterface& bodyInterface;
    ContactListenerImpl contactListener;

    // 注册表（主线程访问；contactListener 内部自带互斥）
    std::unordered_map<Rigidbody*, JPH::BodyID> bodyByRb;
    std::unordered_map<uint32_t, Rigidbody*> rbByBodyId;
    std::unordered_map<Collider*, JPH::BodyID> hostByCollider;
    std::unordered_map<GameObject*, JPH::BodyID> staticBodyByGo;
    std::unordered_map<uint32_t, GameObject*> goByBodyId;   // raycast / TriggerEvent 解析
    std::unordered_set<Collider*> registeredColliders;
    std::unordered_map<const Terrain*, JPH::BodyID> terrainBody;   // V0.5-8 地形高度场体

    // ===== V1.0-3 CharacterController 角色表（主线程访问；固定步内消费）=====
    struct CharacterEntry {
        JPH::CharacterVirtual* character = nullptr;
        glm::vec3 pendingDisplacement{0.0f};   // move() 帧间累积，固定步消费
        bool pendingVelocityOverride = false;  // setVelocity 覆盖标志
        glm::vec3 pendingVelocity{0.0f};
        int layer = 0;   // 角色碰撞层：GO 首个 Collider 的 layer（无 Collider = 0）
    };
    std::unordered_map<CharacterController*, CharacterEntry> characters;
    JPH::CharacterVsCharacterCollisionSimple characterCollision;   // 角色互撞（全部注册于此）
};

PhysicsWorld::Impl::Impl(Scene& s)
    : scene(s), bodyInterface(system.GetBodyInterface()) {
    // Jolt 全局初始化已由 joltInitGuard 在成员构造前完成（DevOps 修复）
    // ===== V1.0-3 层矩阵初始化（必须早于 system.Init——过滤器经指针引用本 Impl 存储）=====
    for (int a = 0; a < 16; ++a)
        for (int b = 0; b < 16; ++b)
            collisionMatrix[a * 16 + b] = true;   // 缺省全通 = 旧行为
    recomputeLayerMasks();
    syncFromSceneMatrix();   // 场景字段 = 权威存储（Edit 面板/序列化写入；构造时读入，Edit 查询即正确）
    pairFilter.matrix = collisionMatrix.data();
    objectVsBpFilter.layerMasks = layerMasks.data();
    system.Init(4096, 0, 4096, 4096, layerInterface, objectVsBpFilter, pairFilter);
    system.SetContactListener(&contactListener);
    system.SetGravity(toJolt(gravity));
    // V1.0-3 物理材质：摩擦/弹性组合规则 v1 = 取平均（§27.2 注释文档化；Jolt 缺省为几何平均）
    system.SetCombineFriction([](const JPH::Body& b1, const JPH::SubShapeID&,
                                 const JPH::Body& b2, const JPH::SubShapeID&) {
        return (b1.GetFriction() + b2.GetFriction()) * 0.5f;
    });
    system.SetCombineRestitution([](const JPH::Body& b1, const JPH::SubShapeID&,
                                    const JPH::Body& b2, const JPH::SubShapeID&) {
        return (b1.GetRestitution() + b2.GetRestitution()) * 0.5f;
    });
}

// ==================== V1.0-3 碰撞层矩阵 ====================

void PhysicsWorld::Impl::recomputeLayerMasks() {
    for (int a = 0; a < 16; ++a) {
        uint32_t m = 0;
        for (int b = 0; b < 16; ++b)
            if (collisionMatrix[a * 16 + b])
                m |= (1u << b);
        layerMasks[a] = m;
    }
}

void PhysicsWorld::Impl::syncFromSceneMatrix() {
    for (int a = 0; a < 16; ++a)
        for (int b = 0; b < 16; ++b)
            collisionMatrix[a * 16 + b] = scene.physicsCollisionMatrix[a][b];
    recomputeLayerMasks();
}

void PhysicsWorld::Impl::refreshBroadphasePairs() {
    // 矩阵变化后全部 body 摘除重入宽相：Jolt 宽相缓存的配对按新掩码重建
    // （否则「关断」只对新配对生效，既有配对继续碰撞——验收 3 互穿必测）。
    // RemoveBody 不销毁 body（位置/速度保留），AddBody 按原运动类型重新激活。
    std::vector<JPH::BodyID> ids;
    ids.reserve(bodyByRb.size() + staticBodyByGo.size() + terrainBody.size());
    for (const auto& [rb, id] : bodyByRb)
        ids.push_back(id);
    for (const auto& [go, id] : staticBodyByGo)
        ids.push_back(id);
    for (const auto& [t, id] : terrainBody)
        ids.push_back(id);
    for (const JPH::BodyID id : ids) {
        if (id.IsInvalid())
            continue;
        bodyInterface.RemoveBody(id);
        // 静态体传 Activate 无效化（Jolt 内部仅对 dynamic 生效）；重入宽相即目的
        bodyInterface.AddBody(id, JPH::EActivation::Activate);
    }
}

PhysicsWorld::Impl::~Impl() {
    teardownAll();
    system.SetContactListener(nullptr);
    // Jolt 全局注销由 joltInitGuard 析构完成（在所有 Jolt 成员析构之后）
}

// ==================== 注册与 body 构建 ====================

void PhysicsWorld::Impl::collectBodyColliders(GameObject* bodyGo, std::vector<Collider*>& out) {
    // 深度优先收集 body GO 子树的 Collider；遇中间 Rigidbody 的子 GO 停止下钻
    // （该子树属于其自身 body）；跳过非激活 GO（activeSelf=false 整棵子树无碰撞体）。
    std::function<void(GameObject*, bool)> visit = [&](GameObject* go, bool isRoot) {
        if (!go->activeSelf())
            return;
        if (!isRoot && go->getComponent<Rigidbody>() != nullptr)
            return;
        for (Collider* c : go->getComponents<Collider>())
            out.push_back(c);
        for (const auto& child : go->children())
            visit(child.get(), false);
    };
    visit(bodyGo, true);
}

Rigidbody* PhysicsWorld::Impl::findHostRigidbody(Collider* c) {
    GameObject* go = c->gameObject();
    if (go == nullptr)
        return nullptr;
    if (auto* rb = go->getComponent<Rigidbody>())
        return rb;
    for (GameObject* p = go->parent(); p != nullptr; p = p->parent())
        if (auto* rb = p->getComponent<Rigidbody>())
            return rb;
    return nullptr;
}

JPH::Ref<JPH::Shape> PhysicsWorld::Impl::buildMeshShape(const MeshCollider* mc,
                                                        const glm::vec3& relScale) {
    if (mc->meshPath.empty()) {
        Logger::warn("[Physics] MeshCollider meshPath 为空（GO: {}），跳过",
                     mc->gameObject() ? mc->gameObject()->name() : "?");
        return nullptr;
    }
    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(mc->meshPath, bytes)) {
        Logger::warn("[Physics] MeshCollider 加载失败: {}（先查 pak 再查磁盘）", mc->meshPath);
        return nullptr;
    }
    const std::string ext = std::filesystem::path(mc->meshPath).extension().string();
    const ModelImportResult result = ModelLoader::loadFromMemory(bytes.data(), bytes.size(), ext);
    if (result.meshes.empty()) {
        Logger::warn("[Physics] MeshCollider 导入失败: {}", mc->meshPath);
        return nullptr;
    }
    // 合并全部子网格（顶点偏移防索引冲突）+ 烘焙相对缩放（Jolt shape 无缩放）
    JPH::VertexList vertices;
    JPH::IndexedTriangleList triangles;
    for (const MeshData& mesh : result.meshes) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        for (const Vertex& v : mesh.vertices)
            vertices.push_back(JPH::Float3(v.position.x * relScale.x, v.position.y * relScale.y,
                                           v.position.z * relScale.z));
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            triangles.push_back(JPH::IndexedTriangle(mesh.indices[i] + base,
                                                     mesh.indices[i + 1] + base,
                                                     mesh.indices[i + 2] + base, 0));
    }
    if (vertices.empty() || triangles.empty()) {
        Logger::warn("[Physics] MeshCollider 网格为空: {}", mc->meshPath);
        return nullptr;
    }
    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    JPH::ShapeSettings::ShapeResult res = settings.Create();
    if (!res.IsValid()) {
        Logger::warn("[Physics] MeshShape 构建失败: {}", res.GetError().c_str());
        return nullptr;
    }
    return res.Get();
}

JPH::Ref<JPH::Shape> PhysicsWorld::Impl::buildPrimitive(const Collider* c,
                                                        const glm::vec3& relScale) {
    // 纯 primitive（原点 = 形状自然中心；center 偏移由调用方在放置姿态中体现）。
    // 缩放烘焙：形状参数乘 relScale（同 GO collider 的 relScale = body GO 局部缩放）。
    // 非均匀缩放近似：sphere 半径取最大轴，capsule 半径取 x/z 较大者、高度取 y 轴。
    if (const auto* box = dynamic_cast<const BoxCollider*>(c)) {
        // convexRadius = 0：盒子精确贴合（默认 0.05 会外扩产生可视缝隙）
        JPH::BoxShapeSettings s(toJolt(box->halfExtents * relScale), 0.0f, nullptr);
        JPH::ShapeSettings::ShapeResult res = s.Create();
        return res.IsValid() ? res.Get() : nullptr;
    }
    if (const auto* sphere = dynamic_cast<const SphereCollider*>(c)) {
        const float r = sphere->radius * std::max(relScale.x, std::max(relScale.y, relScale.z));
        JPH::SphereShapeSettings s(std::max(r, 1e-3f), nullptr);
        JPH::ShapeSettings::ShapeResult res = s.Create();
        return res.IsValid() ? res.Get() : nullptr;
    }
    if (const auto* capsule = dynamic_cast<const CapsuleCollider*>(c)) {
        const float r = capsule->radius * std::max(relScale.x, relScale.z);
        const float halfCyl = std::max(0.0f, (capsule->height * relScale.y) * 0.5f - r);
        JPH::CapsuleShapeSettings s(halfCyl, std::max(r, 1e-3f), nullptr);
        JPH::ShapeSettings::ShapeResult res = s.Create();
        return res.IsValid() ? res.Get() : nullptr;
    }
    if (const auto* mesh = dynamic_cast<const MeshCollider*>(c))
        return buildMeshShape(mesh, relScale);
    return nullptr;
}

JPH::Ref<JPH::Shape> PhysicsWorld::Impl::buildShape(const std::vector<Collider*>& colliders,
                                                    GameObject* bodyGo, bool& outSensor) {
    // sensor = body 级：全部 collider 均为 isTrigger（注释见文件头）
    outSensor = !colliders.empty();
    for (const Collider* c : colliders)
        if (!c->isTrigger)
            outSensor = false;

    const glm::mat4 bodyWorld =
        bodyGo->transform() ? bodyGo->transform()->worldMatrix() : glm::mat4(1.0f);
    const glm::vec3 bodyScale =
        bodyGo->transform() ? bodyGo->transform()->localScale() : glm::vec3(1.0f);

    // 单个 collider：直接放置（center 偏移经 RotatedTranslatedShape 包装）
    if (colliders.size() == 1) {
        const Collider* c = colliders.front();
        glm::vec3 relPos(0.0f);
        glm::quat relRot = glm::identity<glm::quat>();
        glm::vec3 relScale = bodyScale;
        if (c->gameObject() != bodyGo) {
            glm::vec3 skew(0.0f);
            glm::vec4 persp(0.0f, 0.0f, 0.0f, 1.0f);
            glm::decompose(glm::inverse(bodyWorld) * c->gameObject()->transform()->worldMatrix(),
                           relScale, relRot, relPos, skew, persp);
        }
        JPH::Ref<JPH::Shape> primitive = buildPrimitive(c, relScale);
        if (primitive == nullptr)
            return nullptr;
        const glm::vec3 pos = relPos + relRot * (c->center * relScale);
        const glm::quat ident = glm::identity<glm::quat>();
        if (glm::dot(pos, pos) < 1e-12f && glm::abs(glm::dot(relRot, ident)) > 1.0f - 1e-6f)
            return primitive;   // 无偏移无旋转：免包装
        JPH::RotatedTranslatedShapeSettings placed(toJolt(pos), toJolt(relRot),
                                                   primitive.GetPtr());
        JPH::ShapeSettings::ShapeResult res = placed.Create();
        return res.IsValid() ? res.Get() : primitive;
    }

    // 多 shape：StaticCompoundShape 合成（v5 起 CompoundShapeSettings 为抽象基类；
    // 子 shape 姿态含 center 偏移；动态刚体质量属性自动按子 shape 计算）
    JPH::StaticCompoundShapeSettings compound;
    for (const Collider* c : colliders) {
        glm::vec3 relPos(0.0f);
        glm::quat relRot = glm::identity<glm::quat>();
        glm::vec3 relScale = bodyScale;
        if (c->gameObject() != bodyGo) {
            glm::vec3 skew(0.0f);
            glm::vec4 persp(0.0f, 0.0f, 0.0f, 1.0f);
            glm::decompose(glm::inverse(bodyWorld) * c->gameObject()->transform()->worldMatrix(),
                           relScale, relRot, relPos, skew, persp);
        }
        JPH::Ref<JPH::Shape> sub = buildPrimitive(c, relScale);
        if (sub == nullptr)
            continue;
        const glm::vec3 pos = relPos + relRot * (c->center * relScale);
        compound.AddShape(toJolt(pos), toJolt(relRot), sub.GetPtr());
    }
    if (compound.mSubShapes.empty())
        return nullptr;
    JPH::ShapeSettings::ShapeResult res = compound.Create();
    if (!res.IsValid()) {
        Logger::warn("[Physics] CompoundShape 构建失败: {}", res.GetError().c_str());
        return nullptr;
    }
    return res.Get();
}

void PhysicsWorld::Impl::destroyBody(JPH::BodyID id) {
    if (id.IsInvalid())
        return;
    // Jolt 语义：先 RemoveBody（退出活动列表 + 移出 broadphase），再 DestroyBody 释放。
    // 只 DestroyBody 会触发 DestroyBodies 断言（!IsActive && !IsInBroadPhase），且 body
    // 索引复用后 broadphase 跟踪残留 → 后续 AddBody 断言 + 树损坏（2026-11-15 验收暴露）。
    bodyInterface.RemoveBody(id);
    bodyInterface.DestroyBody(id);
    const uint32_t idx = id.GetIndexAndSequenceNumber();
    rbByBodyId.erase(idx);
    goByBodyId.erase(idx);
}

JPH::BodyID PhysicsWorld::Impl::createBodyFor(Rigidbody* rb) {
    GameObject* go = rb->gameObject();
    if (go == nullptr || !go->activeSelf())
        return JPH::BodyID{};
    // V1.0-3 互斥：同 GO 挂 CharacterController → CC 优先，Rigidbody 不建 body
    //（warn 由 CharacterController::onAwake 负责，去重一次）
    if (go->getComponent<CharacterController>() != nullptr)
        return JPH::BodyID{};

    std::vector<Collider*> colliders;
    collectBodyColliders(go, colliders);

    const bool kinematic = rb->isKinematic;
    bool anyMesh = false;
    for (Collider* c : colliders)
        if (dynamic_cast<MeshCollider*>(c) != nullptr)
            anyMesh = true;
    // MeshCollider 仅支持 static/kinematic（Jolt MeshShape 限制）：dynamic body 上跳过并 warn
    if (!kinematic && anyMesh) {
        Logger::warn("[Physics] Rigidbody \"{}\" 为 dynamic，MeshCollider 仅支持 static/kinematic，"
                     "该 MeshCollider 已跳过", go->name());
        std::erase_if(colliders, [](Collider* c) {
            return dynamic_cast<MeshCollider*>(c) != nullptr;
        });
        anyMesh = false;
    }

    bool sensor = false;
    JPH::Ref<JPH::Shape> shape = buildShape(colliders, go, sensor);
    if (shape == nullptr)
        return JPH::BodyID{};   // 无有效 collider（或无 collider）→ 不建 body（addForce 等安全 no-op）

    const Transform* t = go->transform();
    const glm::vec3 pos = t ? t->worldPosition() : glm::vec3(0.0f);
    const glm::quat rot = t ? t->worldRotation() : glm::identity<glm::quat>();

    // ===== V1.0-3 物理材质与碰撞层 =====
    // body 层 = 同 GO collider 优先（多 collider 时取首个），否则子树首个收集 collider；
    // 摩擦/弹性 = 各 collider 平均（组合规则 v1 取平均，注释文档化——§27.2）
    int bodyLayer = 0;
    {
        const std::vector<Collider*> ownColliders = go->getComponents<Collider>();
        if (!ownColliders.empty())
            bodyLayer = ownColliders.front()->layer;
        else if (!colliders.empty())
            bodyLayer = colliders.front()->layer;
    }
    bodyLayer = std::clamp(bodyLayer, 0, PhysicsWorld::kMaxLayers - 1);   // 防御：脚本可写任意值
    float friction = 0.5f, restitution = 0.0f;
    if (!colliders.empty()) {
        float fs = 0.0f, rs = 0.0f;
        for (const Collider* c : colliders) {
            fs += c->friction;
            rs += c->restitution;
        }
        friction = fs / static_cast<float>(colliders.size());
        restitution = rs / static_cast<float>(colliders.size());
    }

    const bool isKin = kinematic || sensor;   // sensor 一律 Kinematic（Transform 驱动）
    const JPH::EMotionType motion = isKin ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;

    JPH::BodyCreationSettings settings(shape.GetPtr(), toJoltR(pos), toJolt(rot), motion,
                                       Layers::objectLayerOf(bodyLayer, /*moving=*/true));
    settings.mIsSensor = sensor;
    settings.mAllowSleeping = !isKin;
    settings.mLinearDamping = std::max(0.0f, rb->linearDamping);
    settings.mAngularDamping = std::max(0.0f, rb->angularDamping);
    settings.mGravityFactor = rb->useGravity ? 1.0f : 0.0f;
    settings.mFriction = friction;
    settings.mRestitution = restitution;
    if (!isKin) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(rb->mass, 1e-3f);
    }

    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (body == nullptr) {
        Logger::error("[Physics] 创建刚体失败（body 数超限？GO: {}）", go->name());
        return JPH::BodyID{};
    }
    const JPH::BodyID id = body->GetID();
    // Jolt 语义：CreateBody 只分配+入管理器；必须再显式 AddBody(id, activation)——
    // AddBody 才把 body 插入 broadphase（参与碰撞），EActivation::Activate 把非静态体
    // 加入活动列表（官方 HelloWorld 用 CreateAndAddBody(settings, Activate) 同义）。
    // 缺失症状（2026-11-15 物理验收暴露）：body 创建成功、motion=Dynamic、重力正确，
    // 但 active=false、位置永不动；且不在 broadphase → 会穿地。
    bodyInterface.AddBody(id, settings.mMotionType == JPH::EMotionType::Static
                                  ? JPH::EActivation::DontActivate
                                  : JPH::EActivation::Activate);
    bodyInterface.SetUserData(id, reinterpret_cast<uint64_t>(go));
    bodyByRb[rb] = id;
    rbByBodyId[id.GetIndexAndSequenceNumber()] = rb;
    goByBodyId[id.GetIndexAndSequenceNumber()] = go;
    for (Collider* c : colliders)
        hostByCollider[c] = id;
    return id;
}

JPH::BodyID PhysicsWorld::Impl::rebuildStaticBody(GameObject* go) {
    if (go == nullptr || !go->activeSelf())
        return JPH::BodyID{};
    // V1.0-3 互斥：同 GO 挂 CharacterController → 其 collider 不建 static body
    //（角色胶囊接管碰撞；否则角色出生即卡进同 GO 的 static 形状被推出）
    if (go->getComponent<CharacterController>() != nullptr)
        return JPH::BodyID{};
    // 销毁旧 static body（含其 collider 的 host 记录）
    if (auto it = staticBodyByGo.find(go); it != staticBodyByGo.end()) {
        const JPH::BodyID oldId = it->second;
        std::vector<Collider*> hosted;
        for (const auto& [c, hid] : hostByCollider)
            if (hid == oldId)
                hosted.push_back(c);
        for (Collider* c : hosted)
            hostByCollider.erase(c);
        destroyBody(oldId);
        staticBodyByGo.erase(it);
    }
    // 同 GO 全部 collider → 多形状合成
    std::vector<Collider*> colliders = go->getComponents<Collider>();
    if (colliders.empty())
        return JPH::BodyID{};
    bool sensor = false;
    JPH::Ref<JPH::Shape> shape = buildShape(colliders, go, sensor);
    if (shape == nullptr)
        return JPH::BodyID{};
    // V1.0-3：层取首个 collider；摩擦/弹性取平均（组合规则 v1 取平均）
    const int bodyLayer = std::clamp(colliders.front()->layer, 0, PhysicsWorld::kMaxLayers - 1);
    float friction = 0.5f, restitution = 0.0f;
    {
        float fs = 0.0f, rs = 0.0f;
        for (const Collider* c : colliders) {
            fs += c->friction;
            rs += c->restitution;
        }
        friction = fs / static_cast<float>(colliders.size());
        restitution = rs / static_cast<float>(colliders.size());
    }
    const Transform* t = go->transform();
    JPH::BodyCreationSettings settings(
        shape.GetPtr(), toJoltR(t ? t->worldPosition() : glm::vec3(0.0f)),
        toJolt(t ? t->worldRotation() : glm::identity<glm::quat>()), JPH::EMotionType::Static,
        Layers::objectLayerOf(bodyLayer, /*moving=*/false));
    settings.mIsSensor = sensor;
    settings.mFriction = friction;
    settings.mRestitution = restitution;
    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (body == nullptr)
        return JPH::BodyID{};
    const JPH::BodyID id = body->GetID();
    // 静态体同样必须 AddBody 进 broadphase（DontActivate；不参与模拟但参与碰撞）
    bodyInterface.AddBody(id, JPH::EActivation::DontActivate);
    bodyInterface.SetUserData(id, reinterpret_cast<uint64_t>(go));
    staticBodyByGo[go] = id;
    goByBodyId[id.GetIndexAndSequenceNumber()] = go;
    for (Collider* c : colliders)
        hostByCollider[c] = id;
    return id;
}

void PhysicsWorld::Impl::destroyStaticBodiesInSubtree(GameObject* root) {
    if (root == nullptr)
        return;
    std::function<void(GameObject*)> visit = [&](GameObject* go) {
        if (auto it = staticBodyByGo.find(go); it != staticBodyByGo.end()) {
            const JPH::BodyID id = it->second;
            std::vector<Collider*> hosted;
            for (const auto& [c, hid] : hostByCollider)
                if (hid == id)
                    hosted.push_back(c);
            for (Collider* c : hosted)
                hostByCollider.erase(c);
            destroyBody(id);
            staticBodyByGo.erase(it);
        }
        for (const auto& child : go->children())
            visit(child.get());
    };
    visit(root);
}

void PhysicsWorld::Impl::rehostOrStatic(Collider* c) {
    // 宿主查找：同 GO → 最近祖先 Rigidbody → 否则 static body
    if (Rigidbody* rb = findHostRigidbody(c)) {
        if (bodyByRb.contains(rb)) {
            // 重建该 body 形状（销毁重建，简化：Play 期结构变更丢失速度，文档化）
            const JPH::BodyID id = bodyByRb.at(rb);
            std::vector<Collider*> hosted;
            for (const auto& [hc, hid] : hostByCollider)
                if (hid == id)
                    hosted.push_back(hc);
            for (Collider* hc : hosted)
                hostByCollider.erase(hc);
            destroyBody(id);
            bodyByRb.erase(rb);
        }
        createBodyFor(rb);
        return;
    }
    rebuildStaticBody(c->gameObject());
}

// ==================== 地形高度场（V0.5-8，c6，§25.6）====================

JPH::BodyID PhysicsWorld::Impl::buildTerrainBody(Terrain* t) {
    const uint32_t res = t->resolution();
    if (res < 2)
        return JPH::BodyID{};
    const GameObject* go = t->gameObject();
    const Transform* tr = t->transform();
    const glm::mat4 world = tr ? tr->worldMatrix() : glm::mat4(1.0f);
    const glm::vec2 size = t->worldSize;
    const float hx = size.x * 0.5f, hz = size.y * 0.5f;
    const float sx = size.x / static_cast<float>(res - 1);
    const float sz = size.y / static_cast<float>(res - 1);

    // Jolt HeightField：surface = offset + scale * (x, samples[y*N+x], z)，x/y ∈ [0, N-1]。
    // offset = 局部网格原点（min 角）的世界位置；scale.y = heightScale/65535（样本 = 原始
    // ushort 高度）。旋转/非均匀缩放不支持（v1 限制，见 PhysicsWorld.h 声明注释）。
    const glm::vec3 offset = glm::vec3(world * glm::vec4(-hx, 0.0f, -hz, 1.0f));
    const glm::vec3 scale(sx, t->heightScale / 65535.0f, sz);

    // 样本经 heightAt（世界米）→ ushort 回推：与渲染/阴影网格逐样本一致
    std::vector<float> samples(static_cast<size_t>(res) * res);
    for (uint32_t z = 0; z < res; ++z) {
        for (uint32_t x = 0; x < res; ++x) {
            const glm::vec3 worldPt =
                glm::vec3(world * glm::vec4(-hx + x * sx, 0.0f, -hz + z * sz, 1.0f));
            const float meters = t->heightAt(worldPt.x, worldPt.z) - offset.y;
            samples[static_cast<size_t>(z) * res + x] =
                std::clamp(meters / std::max(t->heightScale, 1e-3f) * 65535.0f, 0.0f, 65535.0f);
        }
    }

    // shape offset 必须为 0：HeightFieldShape 的表面 = 体位置 + shapeOffset + scale×sample，
    // 若两者都填世界 min 角（-hx,0,-hz）则双重偏移——碰撞面整体平移出世界范围，
    // 物体落在 heightfield 之外直接穿透（2026-11-19 修复，harness phys 模式复现：
    // 方块在 x=2 从 y 10 直坠 -34 无碰撞）。体位置承载世界 min 角即可。
    JPH::HeightFieldShapeSettings settings(samples.data(), JPH::Vec3::sZero(), toJolt(scale), res);
    JPH::ShapeSettings::ShapeResult shapeRes = settings.Create();
    if (!shapeRes.IsValid()) {
        Logger::warn("[Physics] HeightFieldShape 构建失败: {}", shapeRes.GetError().c_str());
        return JPH::BodyID{};
    }
    JPH::BodyCreationSettings bodySettings(shapeRes.Get(), toJoltR(offset),
                                           JPH::Quat::sIdentity(), JPH::EMotionType::Static,
                                           Layers::objectLayerOf(0, /*moving=*/false));   // 地形 = 层 0
    bodySettings.mFriction = 0.5f;   // 地形无摩擦字段（v1；Terrain 无物理材质）
    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (body == nullptr)
        return JPH::BodyID{};
    const JPH::BodyID id = body->GetID();
    bodyInterface.AddBody(id, JPH::EActivation::DontActivate);   // 静态体同样须进 broadphase
    bodyInterface.SetUserData(id, reinterpret_cast<uint64_t>(go));
    goByBodyId[id.GetIndexAndSequenceNumber()] = const_cast<GameObject*>(go);
    return id;
}

void PhysicsWorld::Impl::registerTerrainHeightfield(Terrain* t) {
    if (t == nullptr || tearingDown)
        return;
    if (!t->collisionEnabled) {
        unregisterTerrainHeightfield(t);   // 关闭碰撞：拆除已注册体
        return;
    }
    if (auto it = terrainBody.find(t); it != terrainBody.end()) {
        // 重建路径（笔刷帧末/尺寸变化）：HeightFieldShape 不可原地修改 → 销毁旧体全量新建
        destroyBody(it->second);
        terrainBody.erase(it);
    }
    const JPH::BodyID id = buildTerrainBody(t);
    if (!id.IsInvalid())
        terrainBody[t] = id;
}

void PhysicsWorld::Impl::unregisterTerrainHeightfield(Terrain* t) {
    if (t == nullptr || tearingDown)
        return;   // 批量销毁期：残留 body 由 teardownAll 统一销毁
    auto it = terrainBody.find(t);
    if (it == terrainBody.end())
        return;
    destroyBody(it->second);
    terrainBody.erase(it);
}

void PhysicsWorld::Impl::registerRigidbody(Rigidbody* rb) {
    if (rb == nullptr || bodyByRb.contains(rb) || tearingDown)
        return;   // 批量销毁期不重建（syncFromTransforms 会全量重建）
    // 子树内因「无宿主刚体」而创建的 static body 拆除（collider 随本 body 重建挂接）
    destroyStaticBodiesInSubtree(rb->gameObject());
    createBodyFor(rb);
}

void PhysicsWorld::Impl::unregisterRigidbody(Rigidbody* rb) {
    if (tearingDown)
        return;   // 批量销毁期：残留 body 由 teardownAll 统一销毁
    auto it = bodyByRb.find(rb);
    if (it == bodyByRb.end())
        return;
    const JPH::BodyID id = it->second;
    std::vector<Collider*> hosted;
    for (const auto& [c, hid] : hostByCollider)
        if (hid == id)
            hosted.push_back(c);
    for (Collider* c : hosted)
        hostByCollider.erase(c);
    destroyBody(id);
    bodyByRb.erase(it);
    // 失去宿主的 collider 重新裁决：有其它祖先刚体 → 挂接；否则建 static body
    for (Collider* c : hosted)
        rehostOrStatic(c);
}

void PhysicsWorld::Impl::registerCollider(Collider* c) {
    if (c == nullptr || registeredColliders.contains(c) || tearingDown)
        return;   // 批量销毁期不重挂（syncFromTransforms 会全量重建）
    registeredColliders.insert(c);
    rehostOrStatic(c);
}

void PhysicsWorld::Impl::unregisterCollider(Collider* c) {
    if (c == nullptr)
        return;
    if (tearingDown)
        return;   // 批量销毁期：残留 body 由 teardownAll 统一销毁
    registeredColliders.erase(c);
    auto it = hostByCollider.find(c);
    if (it == hostByCollider.end())
        return;
    const JPH::BodyID id = it->second;
    hostByCollider.erase(c);
    if (auto sit = staticBodyByGo.find(c->gameObject());
        sit != staticBodyByGo.end() && sit->second == id) {
        rebuildStaticBody(c->gameObject());   // 无剩余 collider 时自动销毁
        return;
    }
    for (auto rit = bodyByRb.begin(); rit != bodyByRb.end(); ++rit) {
        if (rit->second != id)
            continue;
        Rigidbody* rb = rit->first;
        // 清其余 host 记录 → 销毁重建
        std::vector<Collider*> hosted;
        for (const auto& [hc, hid] : hostByCollider)
            if (hid == id)
                hosted.push_back(hc);
        for (Collider* hc : hosted)
            hostByCollider.erase(hc);
        destroyBody(id);
        bodyByRb.erase(rit);
        createBodyFor(rb);
        break;
    }
}

void PhysicsWorld::Impl::teardownAll() {
    std::vector<JPH::BodyID> ids;
    ids.reserve(bodyByRb.size() + staticBodyByGo.size() + terrainBody.size());
    for (const auto& [rb, id] : bodyByRb)
        ids.push_back(id);
    for (const auto& [go, id] : staticBodyByGo)
        ids.push_back(id);
    for (const auto& [t, id] : terrainBody)   // V0.5-8 地形高度场体
        ids.push_back(id);
    for (const JPH::BodyID id : ids)
        destroyBody(id);
    bodyByRb.clear();
    hostByCollider.clear();
    staticBodyByGo.clear();
    terrainBody.clear();
    registeredColliders.clear();
    // V1.0-3：全部 CharacterVirtual 一并销毁（角色表清空；下次 Play 全量重建）
    {
        std::vector<CharacterController*> ccs;
        ccs.reserve(characters.size());
        for (const auto& [cc, entry] : characters)
            ccs.push_back(cc);
        for (CharacterController* cc : ccs)
            destroyCharacter(cc);
    }
    synced = false;
}

// ==================== 固定步进 ====================

void PhysicsWorld::Impl::writeKinematics() {
    // kinematic/sensor：每固定步从 Transform 读入世界 TRS → body
    scene.forEachObject([&](GameObject* go) {
        Rigidbody* rb = go->getComponent<Rigidbody>();
        if (rb == nullptr)
            return;
        auto it = bodyByRb.find(rb);
        if (it == bodyByRb.end())
            return;
        {
            JPH::BodyLockRead lock(system.GetBodyLockInterface(), it->second);
            if (!lock.Succeeded() || !lock.GetBody().IsKinematic())
                return;   // 仅 kinematic/sensor 由 Transform 驱动
        }
        const Transform* t = go->transform();
        if (t == nullptr)
            return;
        bodyInterface.SetPositionAndRotation(it->second, toJoltR(t->worldPosition()),
                                             toJolt(t->worldRotation()),
                                             JPH::EActivation::DontActivate);
    });
}

void PhysicsWorld::Impl::writeBackActive() {
    // dynamic body → Transform：世界 TRS 经父级矩阵反解局部 TRS，走
    // setLocalPosition/Rotation/Scale + markDirty 路径（Transform 脏标记自动传播）。
    // 场景深度优先（父先于子）：父级更新后子级用新父矩阵反解。
    scene.forEachObject([&](GameObject* go) {
        Rigidbody* rb = go->getComponent<Rigidbody>();
        if (rb == nullptr)
            return;
        auto it = bodyByRb.find(rb);
        if (it == bodyByRb.end())
            return;
        {
            JPH::BodyLockRead lock(system.GetBodyLockInterface(), it->second);
            if (!lock.Succeeded() || lock.GetBody().IsKinematic() || lock.GetBody().IsStatic())
                return;   // kinematic/sensor/static 不写回（Transform 是前两者的驱动源）
        }
        Transform* t = go->transform();
        if (t == nullptr)
            return;
        JPH::RVec3 pos;
        JPH::Quat rot;
        bodyInterface.GetPositionAndRotation(it->second, pos, rot);
        const glm::vec3 worldPos = toGlm(pos);
        const glm::quat worldRot = glm::normalize(toGlm(rot));

        glm::vec3 localPos = worldPos;
        glm::quat localRot = worldRot;
        if (go->parent() != nullptr && go->parent()->transform() != nullptr) {
            const glm::mat4 local = glm::inverse(go->parent()->transform()->worldMatrix()) *
                                    glm::translate(glm::mat4(1.0f), worldPos) *
                                    glm::mat4_cast(worldRot) *
                                    glm::scale(glm::mat4(1.0f), t->localScale());
            glm::vec3 localScale(1.0f);
            glm::vec3 skew(0.0f);
            glm::vec4 persp(0.0f, 0.0f, 0.0f, 1.0f);
            glm::decompose(local, localScale, localRot, localPos, skew, persp);
        }
        // 分量级 epsilon 比较：避免每步无谓置脏（脏传播会重建全部后代世界矩阵）
        if (glm::distance(t->localPosition(), localPos) > 1e-4f)
            t->setLocalPosition(localPos);
        if (glm::dot(t->localRotation(), localRot) < 0.99999f)
            t->setLocalRotation(localRot);
    });
}

void PhysicsWorld::Impl::flushTriggerEvents() {
    std::vector<SensorContactEvent> events;
    {
        std::lock_guard lock(contactListener.mutex);
        events.swap(contactListener.events);
    }
    for (const SensorContactEvent& e : events) {
        const auto itSensor = goByBodyId.find(e.sensorBody.GetIndexAndSequenceNumber());
        const auto itOther = goByBodyId.find(e.otherBody.GetIndexAndSequenceNumber());
        if (itSensor == goByBodyId.end() || itOther == goByBodyId.end())
            continue;   // body 已销毁（组件卸载）：丢弃
        TriggerEvent ev;
        ev.entered = e.entered;
        ev.gameObjectA = itSensor->second;   // 自身 = 传感器 body 的 GO
        ev.gameObjectB = itOther->second;
        Events::post(ev);   // 延迟事件：Scene::update 的 flushDeferred 统一投递
    }
}

// ==================== PhysicsWorld 公开接口 ====================

PhysicsWorld::PhysicsWorld(Scene& scene) : m_impl(std::make_unique<Impl>(scene)) {}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::beginTeardown() { m_impl->tearingDown = true; }

void PhysicsWorld::step(float dt) {
    Impl& impl = *m_impl;
    if (!impl.synced)
        return;   // 未进入 Play（Scene::start 未调 syncFromTransforms）：不步进
    impl.accumulator = std::min(impl.accumulator + dt, 0.25f);   // dt 上限钳制
    int steps = 0;
    while (impl.accumulator >= kFixedStep && steps < 3) {
        impl.writeKinematics();
        impl.system.Update(kFixedStep, 1, &impl.tempAllocator, &impl.jobSystem);
        // V1.0-3：角色在刚体模拟后步进（对动态体本步最终姿态做碰撞检测）——
        // 与刚体共用同一累加器 → 30/60/144 FPS 行为一致（固定步 1/60）
        impl.stepCharacters(kFixedStep);
        impl.writeBackActive();
        impl.writeBackCharacters();
        impl.accumulator -= kFixedStep;
        ++steps;
    }
    // 诊断（验收期）：前 5 帧打印步数与动态 body 世界位置（确认模拟在跑）
    static int s_stepLogFrames = 0;
    if (s_stepLogFrames < 5 && steps > 0) {
        ++s_stepLogFrames;
        for (const auto& [rb, id] : impl.bodyByRb) {
            const JPH::RVec3 p = impl.bodyInterface.GetPosition(id);
            JPH::BodyLockRead lock(impl.system.GetBodyLockInterface(), id);
            const int motion = lock.Succeeded() ? int(lock.GetBody().GetMotionType()) : -1;
            const bool active = lock.Succeeded() && lock.GetBody().IsActive();
            const float velY = lock.Succeeded() ? lock.GetBody().GetLinearVelocity().GetY() : 0.0f;
            const float gravY = impl.system.GetGravity().GetY();
            const Transform* t = rb->gameObject() ? rb->gameObject()->transform() : nullptr;
            const glm::vec3 tp = t ? t->worldPosition() : glm::vec3(-999.0f);
            Logger::info(
                "[Physics] step={} dt={:.4f} {} pos=({:.2f},{:.2f},{:.2f}) motion={} active={} velY={:.3f} gravY={:.2f} transformY={:.2f}",
                steps, dt, rb->gameObject() ? rb->gameObject()->name() : "?",
                p.GetX(), p.GetY(), p.GetZ(), motion, active, velY, gravY, tp.y);
        }
    }
    if (steps == 3)
        impl.accumulator = 0.0f;   // 防死亡螺旋：丢弃溢出部分
    impl.flushTriggerEvents();     // 帧末统一 post TriggerEvent
}

void PhysicsWorld::syncFromTransforms() {
    Impl& impl = *m_impl;
    impl.teardownAll();
    impl.accumulator = 0.0f;
    impl.tearingDown = false;   // 批量销毁期结束：全量重建恢复注册
    // 0) V1.0-3 碰撞层矩阵：场景字段读入（Edit 面板/脚本写入的环境配置在此生效）
    impl.syncFromSceneMatrix();
    // 1) 全部 Rigidbody body（dynamic/kinematic；内部收集子树 collider；
    //    CC 互斥：同 GO 挂 CharacterController 的 Rigidbody 不建 body——CC 优先）
    for (Rigidbody* rb : impl.scene.findComponents<Rigidbody>())
        impl.createBodyFor(rb);
    // 2) 未被刚体宿主的 collider → static body（同 GO 多 collider 合成一个 body）
    for (Collider* c : impl.scene.findComponents<Collider>()) {
        impl.registeredColliders.insert(c);
        if (!impl.hostByCollider.contains(c))
            impl.rebuildStaticBody(c->gameObject());
    }
    // 3) 地形高度场静态体（V0.5-8，c6；内部按 collisionEnabled 裁决）
    for (Terrain* t : impl.scene.findComponents<Terrain>())
        impl.registerTerrainHeightfield(t);
    // 4) V1.0-3 CharacterController：全量创建 CharacterVirtual（位置 = Transform 世界位置）
    impl.synced = true;   // 先于角色创建置位：registerCharacter 的「Play 中挂载」分支即走此路径
    for (CharacterController* cc : impl.scene.findComponents<CharacterController>())
        impl.createCharacterFor(cc);
    // 诊断（验收期）：打印 body 统计，冒烟/验收可据此确认物理世界非空
    Logger::info("[Physics] syncFromTransforms 完成：动态/运动 body={}，static body={}，"
                 "terrain heightfield={}，character={}，synced=true",
                 impl.rbByBodyId.size(), impl.staticBodyByGo.size(), impl.terrainBody.size(),
                 impl.characters.size());
}

// ==================== V1.0-3 CharacterController 宿主 ====================
//
// 位置约定：Transform 世界位置 = 胶囊中心（CharacterVirtual 位置同义，形状无偏移；
// 站立地面时胶囊中心在地面上方 height/2 处）。旋转 v1 忽略（直立胶囊，Quat 恒等）。
// 步进模型（每固定步，1/60s）：
//   move() 帧间累积位移 → 本步速度 = 位移/kFixedStep（30/60/144 FPS 位移一致：多步帧
//   首步消费全部缓冲，后续步速度 0）；setVelocity 覆盖优先级最高；
//   重力不自积分（CharacterVirtual 语义）→ 非支撑态 v += gravity*gravityScale*dt，
//   支撑态清下行速度（StickToFloor 贴地，斜坡速度不衰减为 0——验收 1）；
//   ExtendedUpdate = Update + StickToFloor + WalkStairs（楼梯步进 = stepHeight）。
// 层过滤：角色层 = GO 首个 Collider 的 layer（无 = 0），矩阵第 ccLayer 行决定与各层开关。

JPH::CharacterVirtual* PhysicsWorld::Impl::createCharacterFor(CharacterController* cc) {
    GameObject* go = cc->gameObject();
    if (go == nullptr)
        return nullptr;
    // 胶囊形状：圆柱半高 = height/2 - radius（总高 = height，Unity 语义）
    const float r = std::max(cc->radius, 1e-3f);
    const float halfCyl = std::max(0.0f, cc->height * 0.5f - r);
    JPH::CapsuleShapeSettings shapeSettings(halfCyl, r, nullptr);
    JPH::ShapeSettings::ShapeResult shapeRes = shapeSettings.Create();
    if (!shapeRes.IsValid())
        return nullptr;

    int layer = 0;
    for (Collider* c : go->getComponents<Collider>()) {
        layer = c->layer;
        break;
    }
    layer = std::clamp(layer, 0, PhysicsWorld::kMaxLayers - 1);   // 防御：脚本可写任意值

    JPH::CharacterVirtualSettings settings;
    settings.mShape = shapeRes.Get();
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(std::clamp(cc->slopeLimitDeg, 0.0f, 89.9f));
    settings.mMass = std::max(cc->mass, 1e-3f);
    settings.mEnhancedInternalEdgeRemoval = true;   // 网格内边幽灵接触消除（楼梯/坡面平滑）

    const Transform* t = go->transform();
    const glm::vec3 pos = t ? t->worldPosition() : glm::vec3(0.0f);
    auto* cv = new JPH::CharacterVirtual(&settings, toJoltR(pos), JPH::Quat::sIdentity(),
                                         reinterpret_cast<uint64_t>(go), &system);
    cv->SetCharacterVsCharacterCollision(&characterCollision);
    CharacterEntry entry;
    entry.character = cv;
    entry.layer = layer;
    characters[cc] = entry;
    return cv;
}

void PhysicsWorld::Impl::destroyCharacter(CharacterController* cc) {
    auto it = characters.find(cc);
    if (it == characters.end())
        return;
    if (it->second.character != nullptr) {
        characterCollision.Remove(it->second.character);
        delete it->second.character;
    }
    characters.erase(it);
}

void PhysicsWorld::Impl::stepCharacters(float dt) {
    for (auto& [cc, entry] : characters) {
        JPH::CharacterVirtual* cv = entry.character;
        if (cv == nullptr)
            continue;
        // 目标速度：setVelocity 覆盖 > move 缓冲 > 既有速度
        glm::vec3 v;
        if (entry.pendingVelocityOverride) {
            v = entry.pendingVelocity;
            entry.pendingVelocityOverride = false;
        } else if (glm::dot(entry.pendingDisplacement, entry.pendingDisplacement) > 1e-12f) {
            v = entry.pendingDisplacement / dt;
            entry.pendingDisplacement = glm::vec3(0.0f);
        } else {
            v = toGlm(cv->GetLinearVelocity());
        }
        // 重力积分（CharacterVirtual 不自积分；gravityScale 缩放）
        const glm::vec3 g = gravity * std::max(cc->gravityScale, 0.0f);
        if (!cv->IsSupported()) {
            v += g * dt;
        } else if (v.y < 0.0f) {
            v.y = 0.0f;   // 地面：清下行速度（StickToFloor 贴地；跳跃上行速度保留）
        }
        cv->SetLinearVelocity(toJolt(v));

        // 楼梯步进/贴地参数（stepHeight ≤ 0 = 关闭楼梯步进，Jolt 语义）
        JPH::CharacterVirtual::ExtendedUpdateSettings ups;
        ups.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.5f, 0.0f);
        ups.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(cc->stepHeight, 0.0f), 0.0f);
        MatrixRowObjectFilter layerFilter;
        layerFilter.row = &collisionMatrix[entry.layer * 16];
        cv->ExtendedUpdate(dt, toJolt(g), ups, JPH::BroadPhaseLayerFilter{}, layerFilter,
                           JPH::BodyFilter{}, JPH::ShapeFilter{}, tempAllocator);
    }
}

void PhysicsWorld::Impl::writeBackCharacters() {
    for (auto& [cc, entry] : characters) {
        if (entry.character == nullptr)
            continue;
        GameObject* go = cc->gameObject();
        Transform* t = (go != nullptr) ? go->transform() : nullptr;
        if (t == nullptr)
            continue;
        const glm::vec3 worldPos = toGlm(entry.character->GetPosition());
        glm::vec3 localPos = worldPos;
        if (go->parent() != nullptr && go->parent()->transform() != nullptr) {
            // 父级矩阵反解局部位置（与 writeBackActive 同约定；旋转/缩放不参与）
            const glm::mat4 local = glm::inverse(go->parent()->transform()->worldMatrix()) *
                                    glm::translate(glm::mat4(1.0f), worldPos);
            localPos = glm::vec3(local[3]);
        }
        if (glm::distance(t->localPosition(), localPos) > 1e-4f)
            t->setLocalPosition(localPos);
    }
}

bool PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                           float maxDistance, RaycastHit& outHit) {
    outHit = RaycastHit{};
    const float len = glm::length(direction);
    if (len < 1e-6f || maxDistance <= 0.0f)
        return false;
    Impl& impl = *m_impl;
    const glm::vec3 dir = direction / len;
    const JPH::RRayCast ray(toJoltR(origin), toJolt(dir) * maxDistance);
    JPH::RayCastResult result;
    const JPH::NarrowPhaseQuery& query = impl.system.GetNarrowPhaseQuery();
    const JPH::BroadPhaseLayerFilter bplFilter{};
    const JPH::ObjectLayerFilter objFilter{};
    const JPH::BodyFilter bodyFilter{};
    if (!query.CastRay(ray, result, bplFilter, objFilter, bodyFilter))
        return false;
    const auto it = impl.goByBodyId.find(result.mBodyID.GetIndexAndSequenceNumber());
    if (it == impl.goByBodyId.end())
        return false;
    outHit.hit = true;
    outHit.gameObject = it->second;
    outHit.hitObject = it->second;   // V1.0-3：与 gameObject 同值（旧名保留兼容）
    outHit.distance = result.mFraction * maxDistance;
    outHit.point = origin + dir * outHit.distance;
    // 法线：Body::GetWorldSpaceSurfaceNormal（读锁内访问）
    const JPH::BodyLockRead lock(impl.system.GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
        outHit.normal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2,
                                                                        toJoltR(outHit.point)));
    return true;
}

// ==================== V1.0-3 查询 API（§27.2）====================

bool PhysicsWorld::raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                              float maxDistance, const RaycastFilter& filter,
                              std::vector<RaycastHit>& outHits) {
    outHits.clear();
    const float len = glm::length(direction);
    if (len < 1e-6f || maxDistance <= 0.0f)
        return false;
    Impl& impl = *m_impl;
    const glm::vec3 dir = direction / len;
    const JPH::RRayCast ray(toJoltR(origin), toJolt(dir) * maxDistance);
    const JPH::RayCastSettings settings;   // 与单命中 raycast 同缺省（背面忽略）
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    MaskObjectLayerFilter layerFilter;
    layerFilter.mask = filter.layerMask;
    IgnoreBodyFilter bodyFilter;
    bodyFilter.goByBodyId = &impl.goByBodyId;
    bodyFilter.ignore = filter.ignore;
    impl.system.GetNarrowPhaseQuery().CastRay(ray, settings, collector, JPH::BroadPhaseLayerFilter{},
                                              layerFilter, bodyFilter);
    if (collector.mHits.empty())
        return false;
    collector.Sort();   // 距离升序（fraction 升序）——验收 2：3 命中堆叠物按距离排序
    outHits.reserve(collector.mHits.size());
    for (const JPH::RayCastResult& r : collector.mHits) {
        const auto it = impl.goByBodyId.find(r.mBodyID.GetIndexAndSequenceNumber());
        if (it == impl.goByBodyId.end())
            continue;   // body 已销毁：丢弃
        RaycastHit hit;
        hit.hit = true;
        hit.gameObject = it->second;
        hit.hitObject = it->second;
        hit.distance = r.mFraction * maxDistance;
        hit.point = origin + dir * hit.distance;
        const JPH::BodyLockRead lock(impl.system.GetBodyLockInterface(), r.mBodyID);
        if (lock.Succeeded())
            hit.normal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(
                r.mSubShapeID2, toJoltR(hit.point)));
        outHits.push_back(hit);
    }
    return !outHits.empty();
}

bool PhysicsWorld::Impl::overlapWithShape(const JPH::Shape* shape, const glm::vec3& center,
                                          const PhysicsWorld::RaycastFilter& filter,
                                          std::vector<GameObject*>& outObjects) {
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    MaskObjectLayerFilter layerFilter;
    layerFilter.mask = filter.layerMask;
    IgnoreBodyFilter bodyFilter;
    bodyFilter.goByBodyId = &goByBodyId;
    bodyFilter.ignore = filter.ignore;
    const JPH::CollideShapeSettings settings;   // 缺省（含背面）
    const JPH::RMat44 com = JPH::RMat44::sTranslation(toJoltR(center));
    system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1.0f), com, settings,
                                              JPH::RVec3::sZero(), collector,
                                              JPH::BroadPhaseLayerFilter{}, layerFilter,
                                              bodyFilter);
    if (collector.mHits.empty())
        return false;
    // 去重（多子 shape 命中同一 body → 只列一次 GO；含触发器）
    std::unordered_set<GameObject*> seen;
    for (const JPH::CollideShapeResult& r : collector.mHits) {
        const auto it = goByBodyId.find(r.mBodyID2.GetIndexAndSequenceNumber());
        if (it == goByBodyId.end())
            continue;
        if (seen.insert(it->second).second)
            outObjects.push_back(it->second);
    }
    return !outObjects.empty();
}

bool PhysicsWorld::overlapSphere(const glm::vec3& center, float radius,
                                 const RaycastFilter& filter,
                                 std::vector<GameObject*>& outObjects) {
    outObjects.clear();
    if (radius <= 0.0f)
        return false;
    JPH::SphereShapeSettings shapeSettings(std::max(radius, 1e-3f), nullptr);
    JPH::ShapeSettings::ShapeResult shapeRes = shapeSettings.Create();
    if (!shapeRes.IsValid())
        return false;
    return m_impl->overlapWithShape(shapeRes.Get(), center, filter, outObjects);
}

bool PhysicsWorld::overlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
                              const RaycastFilter& filter,
                              std::vector<GameObject*>& outObjects) {
    outObjects.clear();
    if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
        return false;
    // convexRadius = 0：盒精确贴合（与 BoxCollider 同口径）
    JPH::BoxShapeSettings shapeSettings(toJolt(halfExtents), 0.0f, nullptr);
    JPH::ShapeSettings::ShapeResult shapeRes = shapeSettings.Create();
    if (!shapeRes.IsValid())
        return false;
    return m_impl->overlapWithShape(shapeRes.Get(), center, filter, outObjects);
}

// ==================== V1.0-3 碰撞层 API（§27.2；配置 = 场景级 environment.physics）====================

void PhysicsWorld::setLayerName(int layer, const std::string& name) {
    if (layer < 0 || layer >= kMaxLayers)
        return;
    // scene.physicsLayerNames = 权威存储（序列化/面板共享）；不足 16 时补齐（防御）
    while (m_impl->scene.physicsLayerNames.size() < size_t(kMaxLayers))
        m_impl->scene.physicsLayerNames.push_back(std::string());
    m_impl->scene.physicsLayerNames[size_t(layer)] = name;
}

const std::string& PhysicsWorld::layerName(int layer) {
    static const std::string s_empty;
    if (layer < 0 || layer >= kMaxLayers)
        return s_empty;
    auto& names = m_impl->scene.physicsLayerNames;
    if (size_t(layer) >= names.size())
        return s_empty;
    return names[size_t(layer)];
}

void PhysicsWorld::setCollisionEnabled(int a, int b, bool enabled) {
    if (a < 0 || a >= kMaxLayers || b < 0 || b >= kMaxLayers)
        return;
    Impl& impl = *m_impl;
    // 对称写：矩阵对称不变量（matrix[a][b] == matrix[b][a]，UI 只展示三角区）
    impl.collisionMatrix[a * 16 + b] = enabled;
    impl.collisionMatrix[b * 16 + a] = enabled;
    impl.recomputeLayerMasks();
    // 同步场景字段（序列化持久化 + 面板读取）
    impl.scene.physicsCollisionMatrix[a][b] = enabled;
    impl.scene.physicsCollisionMatrix[b][a] = enabled;
    // Play 中变更：既有宽相配对按新矩阵重建（否则关断不互穿——验收 3）
    impl.refreshBroadphasePairs();
}

bool PhysicsWorld::collisionEnabled(int a, int b) const {
    if (a < 0 || a >= kMaxLayers || b < 0 || b >= kMaxLayers)
        return false;
    return m_impl->collisionMatrix[a * 16 + b];
}

// ==================== V1.0-3 CharacterController 桥（§27.2；CC.cpp 经 friend 调用）====================

void PhysicsWorld::registerCharacter(CharacterController* cc) {
    if (cc == nullptr || m_impl->characters.contains(cc) || m_impl->tearingDown)
        return;   // 批量销毁期：角色由 teardownAll 统一销毁，syncFromTransforms 全量重建
    // CC 优先互斥（Play 中动态挂 CC）：拆除该 GO 已建 Rigidbody body
    if (GameObject* go = cc->gameObject()) {
        if (Rigidbody* rb = go->getComponent<Rigidbody>()) {
            if (auto it = m_impl->bodyByRb.find(rb); it != m_impl->bodyByRb.end()) {
                std::vector<Collider*> hosted;
                for (const auto& [hc, hid] : m_impl->hostByCollider)
                    if (hid == it->second)
                        hosted.push_back(hc);
                for (Collider* hc : hosted)
                    m_impl->hostByCollider.erase(hc);
                m_impl->destroyBody(it->second);
                m_impl->bodyByRb.erase(it);
                Logger::warn("[Physics] GO \"{}\" 动态挂载 CharacterController：Rigidbody body 已拆除"
                             "（CC 优先互斥）", go->name());
            }
        }
    }
    if (!m_impl->synced)
        return;   // Edit：待 syncFromTransforms 全量创建（synced 在角色创建前已置位，见 sync）
    m_impl->createCharacterFor(cc);
}

void PhysicsWorld::unregisterCharacter(CharacterController* cc) {
    m_impl->destroyCharacter(cc);
}

void PhysicsWorld::characterMove(CharacterController* cc, const glm::vec3& displacement) {
    auto it = m_impl->characters.find(cc);
    if (it == m_impl->characters.end())
        return;   // Edit / 角色未创建：no-op
    it->second.pendingDisplacement += displacement;
}

bool PhysicsWorld::characterVelocity(const CharacterController* cc, glm::vec3& out) const {
    auto it = m_impl->characters.find(const_cast<CharacterController*>(cc));
    if (it == m_impl->characters.end() || it->second.character == nullptr)
        return false;
    out = toGlm(it->second.character->GetLinearVelocity());
    return true;
}

bool PhysicsWorld::setCharacterVelocity(CharacterController* cc, const glm::vec3& v) {
    auto it = m_impl->characters.find(cc);
    if (it == m_impl->characters.end())
        return false;
    it->second.pendingVelocity = v;
    it->second.pendingVelocityOverride = true;
    return true;
}

void PhysicsWorld::characterSetPosition(CharacterController* cc, const glm::vec3& worldPos) {
    auto it = m_impl->characters.find(cc);
    if (it == m_impl->characters.end() || it->second.character == nullptr)
        return;   // Edit / 角色未创建：no-op
    // 物理级传送：CharacterVirtual::SetPosition 直设（复活/瞬移——只改 Transform 会被
    // 下一固定步 writeBackCharacters 覆盖回旧位置，波 3 #10 掉落复活 bug 根因）
    it->second.character->SetPosition(toJolt(worldPos));
    it->second.pendingDisplacement = glm::vec3(0.0f);
    it->second.pendingVelocity = glm::vec3(0.0f);
    it->second.pendingVelocityOverride = true;
}

bool PhysicsWorld::characterGrounded(const CharacterController* cc) const {
    auto it = m_impl->characters.find(const_cast<CharacterController*>(cc));
    return it != m_impl->characters.end() && it->second.character != nullptr &&
           it->second.character->IsSupported();
}

void PhysicsWorld::setGravity(const glm::vec3& g) {
    m_impl->gravity = g;
    m_impl->system.SetGravity(toJolt(g));
}

glm::vec3 PhysicsWorld::gravity() const {
    return m_impl->gravity;
}

void PhysicsWorld::debugDraw(const std::function<void(const glm::vec3&, const glm::vec3&)>& line) {
    Impl& impl = *m_impl;
    if (!line)
        return;
    DebugLineCollector collector(line);
    JPH::BodyManager::DrawSettings settings;
    settings.mDrawShape = true;
    settings.mDrawShapeWireframe = true;   // 只提取线框线段（实心三角形被收集器忽略）
    settings.mDrawShapeColor = JPH::BodyManager::EShapeColor::MotionTypeColor;
    impl.system.DrawBodies(settings, &collector);
}

// ==================== 组件注册与刚体接口 ====================

void PhysicsWorld::registerRigidbody(Rigidbody* rb) { m_impl->registerRigidbody(rb); }
void PhysicsWorld::unregisterRigidbody(Rigidbody* rb) { m_impl->unregisterRigidbody(rb); }
void PhysicsWorld::registerCollider(Collider* c) { m_impl->registerCollider(c); }
void PhysicsWorld::unregisterCollider(Collider* c) { m_impl->unregisterCollider(c); }

// ==================== 地形高度场（V0.5-8，c6，§25.6 追加）====================

void PhysicsWorld::registerTerrainHeightfieldInternal(Terrain* t) {
    m_impl->registerTerrainHeightfield(t);
}

void PhysicsWorld::unregisterTerrainHeightfieldInternal(Terrain* t) {
    m_impl->unregisterTerrainHeightfield(t);
}

void PhysicsWorld::addForce(Rigidbody* rb, const glm::vec3& force) {
    if (rb == nullptr)
        return;
    const auto it = m_impl->bodyByRb.find(rb);
    if (it == m_impl->bodyByRb.end())
        return;   // 无 Collider → 无 body：安全 no-op
    m_impl->bodyInterface.AddForce(it->second, toJolt(force));
}

void PhysicsWorld::addImpulse(Rigidbody* rb, const glm::vec3& impulse) {
    if (rb == nullptr)
        return;
    const auto it = m_impl->bodyByRb.find(rb);
    if (it == m_impl->bodyByRb.end())
        return;
    m_impl->bodyInterface.AddImpulse(it->second, toJolt(impulse));
}

glm::vec3 PhysicsWorld::velocity(const Rigidbody* rb) const {
    if (rb == nullptr)
        return glm::vec3(0.0f);
    const auto it = m_impl->bodyByRb.find(const_cast<Rigidbody*>(rb));
    if (it == m_impl->bodyByRb.end())
        return glm::vec3(0.0f);
    return toGlm(m_impl->bodyInterface.GetLinearVelocity(it->second));
}

void PhysicsWorld::setVelocity(Rigidbody* rb, const glm::vec3& v) {
    if (rb == nullptr)
        return;
    const auto it = m_impl->bodyByRb.find(rb);
    if (it == m_impl->bodyByRb.end())
        return;
    m_impl->bodyInterface.SetLinearVelocity(it->second, toJolt(v));   // 内部自动唤醒睡眠 body
}

// ==================== 地形高度场自由函数（§25.6 追加；PhysicsWorld.h 文件尾声明）====================
// 定位服务：terrain → GameObject → Scene → PhysicsWorld；无 scene/无 physics 时安全
// no-op（组件未入场景窗口期）。私有入口访问经 friend 声明（PhysicsWorld.h）。

void registerTerrainHeightfield(Terrain* terrain) {
    if (terrain == nullptr)
        return;
    const GameObject* go = terrain->gameObject();
    PhysicsWorld* pw = (go != nullptr && go->scene() != nullptr) ? go->scene()->physics() : nullptr;
    if (pw != nullptr)
        pw->registerTerrainHeightfieldInternal(terrain);
}

void unregisterTerrainHeightfield(Terrain* terrain) {
    if (terrain == nullptr)
        return;
    const GameObject* go = terrain->gameObject();
    PhysicsWorld* pw = (go != nullptr && go->scene() != nullptr) ? go->scene()->physics() : nullptr;
    if (pw != nullptr)
        pw->unregisterTerrainHeightfieldInternal(terrain);
}

}  // namespace NovaEngine
