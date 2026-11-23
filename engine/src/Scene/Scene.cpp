#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Physics/PhysicsWorld.h>
#include <NovaEngine/Physics/Physics2D.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Profiling/Profiler.h>
#include "../Serialization/SerializationInternals.h"   // scriptTypeName：脚本类名裁剪单一来源
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <unordered_map>

namespace NovaEngine {

namespace {
// 深度优先收集子树全部对象（用于 flushDestroyQueue 清理索引）
void collectSubtree(GameObject* root, std::vector<GameObject*>& out) {
    out.push_back(root);
    for (auto& child : root->children())
        collectSubtree(child.get(), out);
}

// V1.0-1 Profiler 脚本档（§26.2，cR 波 1）：Scene::update 帧内按脚本类名累计
// onUpdate 耗时，帧末上报 pushSection("script:"+类名)。复用此表避免每帧分配
//（主线程专用，无并发；Scene::update 仅在 Play 态调用）。
thread_local std::unordered_map<std::string, float> s_scriptOnUpdateMs;
}  // namespace

Scene::Scene(const std::string& name)
    : m_name(name), m_physics(std::make_unique<PhysicsWorld>(*this)) {
    // ===== V1.0-3 碰撞层矩阵缺省（§27.2 骨架日布）：全通矩阵 + 默认层名 =====
    // 旧场景无 environment.physics 段时读时保持本缺省（零警告）
    for (int a = 0; a < 16; ++a)
        for (int b = 0; b < 16; ++b)
            physicsCollisionMatrix[a][b] = true;
    physicsLayerNames.clear();
    physicsLayerNames.reserve(16);
    for (int i = 0; i < 16; ++i)
        physicsLayerNames.push_back(i == 0 ? "Default" : "Layer" + std::to_string(i));
}

// 析构定义在 .cpp：unique_ptr<PhysicsWorld> 需要 Impl 完整类型（PhysicsWorld.h 为 pimpl）
Scene::~Scene() {
    // 批量销毁旗标（2026-11-15 物理验收修复）：先于 GameObject 析构置位——
    // 组件析构期的 unregister 不再重建/重挂（防对已析构 GO 的 UAF），残留 body 由
    // PhysicsWorld 析构的 teardownAll 统一销毁
    if (m_physics)
        m_physics->beginTeardown();
    m_rootObjects.clear();   // 显式清空：GameObject 析构先于 m_physics（原 =default 亦此序）
}

PhysicsWorld* Scene::physics() const {
    return m_physics.get();
}

const std::string& Scene::name() const {
    return m_name;
}

void Scene::setName(const std::string& name) {
    m_name = name;
}

GameObject* Scene::createGameObject(const std::string& name, GameObject* parent) {
    auto go = std::make_unique<GameObject>(UUID::generate(), name);
    GameObject* raw = go.get();
    raw->m_scene = this;
    // 注册进 m_objectById；ID 冲突（概率可忽略）则重新生成
    while (m_objectById.contains(raw->m_id))
        raw->m_id = UUID::generate();
    m_objectById[raw->m_id] = raw;

    if (parent != nullptr && parent->m_scene == this) {
        parent->m_children.push_back(std::move(go));
        raw->m_parent = parent;   // 修复 2026-11-16：m_parent 必须同步设置——
        // 否则 parent() 返回 nullptr，序列化快照把 parent 写成 0，
        // Play 退出还原后子对象全部挂根（层级被拍平）
    } else {
        if (parent != nullptr)
            Logger::warn("createGameObject：父级不属于当前场景，对象已挂到根层级");
        m_rootObjects.push_back(std::move(go));
    }
    raw->addComponent<Transform>();  // 自动附带 Transform
    return raw;
}

GameObject* Scene::instantiate(uint64_t id, const std::string& name) {
    // 反序列化用：不自动加 Transform（组件由 SceneSerializer 按 JSON 逐个挂载）
    auto go = std::make_unique<GameObject>(id, name);
    GameObject* raw = go.get();
    raw->m_scene = this;
    // ID 原样保留；与现有对象冲突（或为无效值 0）才重新生成
    if (raw->m_id == UUID::kInvalid || m_objectById.contains(raw->m_id)) {
        Logger::warn("instantiate：对象 ID {} 冲突或无效，已重新生成", raw->m_id);
        do {
            raw->m_id = UUID::generate();
        } while (m_objectById.contains(raw->m_id));
    }
    m_objectById[raw->m_id] = raw;
    m_rootObjects.push_back(std::move(go));  // 先挂根，反序列化第二遍再按 parent 字段重建层级
    return raw;
}

void Scene::destroyGameObject(uint64_t id) {
    // 入销毁队列（延迟删除，避免 update 遍历中迭代器失效）
    if (m_objectById.contains(id))
        m_destroyQueue.push_back(id);
}

void Scene::flushDestroyQueue() {
    if (m_destroyQueue.empty())
        return;
    std::vector<uint64_t> queue;
    queue.swap(m_destroyQueue);

    for (uint64_t id : queue) {
        auto it = m_objectById.find(id);
        if (it == m_objectById.end())
            continue;  // 已随祖先一并删除
        GameObject* go = it->second;

        // 子树整体删除：先把全部成员的 ID 从索引中移除
        std::vector<GameObject*> dying;
        collectSubtree(go, dying);
        for (GameObject* d : dying)
            m_objectById.erase(d->m_id);

        // 从父级 children（或根列表）摘除；unique_ptr 释放时子树随级联析构
        if (go->m_parent != nullptr) {
            auto& list = go->m_parent->m_children;
            std::erase_if(list, [go](const std::unique_ptr<GameObject>& c) { return c.get() == go; });
        } else {
            std::erase_if(m_rootObjects, [go](const std::unique_ptr<GameObject>& c) { return c.get() == go; });
        }
    }
}

GameObject* Scene::findGameObject(uint64_t id) const {
    auto it = m_objectById.find(id);
    return (it != m_objectById.end()) ? it->second : nullptr;
}

std::vector<GameObject*> Scene::findGameObjectsByName(const std::string& name) const {
    std::vector<GameObject*> out;
    forEachObject([&](GameObject* go) {
        if (go->name() == name)
            out.push_back(go);
    });
    return out;
}

std::vector<GameObject*> Scene::rootObjects() const {
    std::vector<GameObject*> out;
    out.reserve(m_rootObjects.size());
    for (const auto& go : m_rootObjects)
        out.push_back(go.get());
    return out;
}

void Scene::forEachObject(const std::function<void(GameObject*)>& fn) const {
    // 深度优先（先根后子）
    std::function<void(GameObject*)> visit = [&](GameObject* go) {
        fn(go);
        for (auto& child : go->m_children)
            visit(child.get());
    };
    for (const auto& root : m_rootObjects)
        visit(root.get());
}

void Scene::update(float dt) {
    // Play 态帧序骨架（V0.3 波 2 定案，§15）：
    //   flush 销毁队列 → 物理固定步进 → onUpdate 广播 → 协程 advance(false)
    //   → Events::flushDeferred → 协程 advance(true)
    // Edit 态不调 update（编辑器只在 Play 调）；物理 step 在广播前（脚本当帧读到新物理状态）。
    flushDestroyQueue();
    if (m_physics) m_physics->step(dt);   // 固定步进累加器（kFixedStep=1/60，每帧最多 3 步）

    // V1.0-1 Profiler 脚本档（§26.2，cR 波 1）：广播循环内按脚本类型名累计 onUpdate 耗时。
    // 判别：类名（SerializationInternals::scriptTypeName，与序列化/热重载同口径）在
    // ScriptRegistry 注册 = 脚本组件——同时覆盖 Inspector 挂载（共用 Behaviour typeId，
    // 裁定 #4）与 C++ 侧 addComponent<MyScript>() 两条路径；内置 Behaviour 派生组件
    // （Button/Slider 等）不在 ScriptRegistry，天然排除。
    s_scriptOnUpdateMs.clear();
    std::function<void(GameObject*)> broadcast = [&](GameObject* go) {
        if (go->activeSelf()) {
            for (auto& comp : go->m_components) {
                Behaviour* b = dynamic_cast<Behaviour*>(comp.get());
                // 裁定：Behaviour 的 enabled=false 时跳过 onUpdate
                if (b != nullptr && !b->enabled)
                    continue;
                if (b != nullptr) {
                    const std::string typeName =
                        SerializationInternals::scriptTypeName(comp.get());
                    if (ScriptRegistry::exists(typeName)) {
                        // 脚本组件：计时并累计到类名桶
                        const auto t0 = std::chrono::steady_clock::now();
                        comp->onUpdate(dt);
                        s_scriptOnUpdateMs[typeName] +=
                            std::chrono::duration<float, std::milli>(
                                std::chrono::steady_clock::now() - t0).count();
                        continue;
                    }
                }
                comp->onUpdate(dt);
            }
            for (auto& child : go->m_children)
                broadcast(child.get());
        }
        // 非激活对象整棵子树跳过（等价 activeInHierarchy 语义）
    };
    for (const auto& root : m_rootObjects)
        broadcast(root.get());

    // 上报脚本档分段（同名脚本多实例合并为一帧总耗时；64 段上限合并语义在 Profiler 内）
    for (const auto& [className, ms] : s_scriptOnUpdateMs) {
        char sectionName[128];
        std::snprintf(sectionName, sizeof(sectionName), "script:%s", className.c_str());
        Profiler::pushSection(sectionName, ms);
    }

    // 2D 碰撞求解（V0.5-10 波 2 2b 合波接线，§25.8 固定点）：onUpdate 广播之后、协程推进之前——
    // 事件同步 publish，脚本本帧即可收到；2D 与 3D（Jolt，广播前步进）顺序互不依赖
    Physics2D::step(*this);

    // 协程推进（普通 yield：NextFrame/AfterSeconds/…）——advanceCoroutines 是 Behaviour 私有，
    // Scene 是 friend（§14.6 帧内顺序第 c 步）。裁定：enabled=false 不暂停协程（与 Unity 一致）；
    // 非激活 GO 的协程照常推进（已知差异，已文档化）
    std::function<void(GameObject*)> advance = [&](GameObject* go) {
        for (auto& comp : go->m_components)
            if (auto* b = dynamic_cast<Behaviour*>(comp.get()))
                b->advanceCoroutines(dt, false);
        for (auto& child : go->m_children)
            advance(child.get());
    };
    for (const auto& root : m_rootObjects)
        advance(root.get());

    NovaEngine::Events::flushDeferred();   // 延迟事件投递（物理 TriggerEvent 等在此送达脚本）

    // 协程推进（WaitForEndOfFrame）：第 e 步（§15）
    std::function<void(GameObject*)> advanceEnd = [&](GameObject* go) {
        for (auto& comp : go->m_components)
            if (auto* b = dynamic_cast<Behaviour*>(comp.get()))
                b->advanceCoroutines(0.0f, true);
        for (auto& child : go->m_children)
            advanceEnd(child.get());
    };
    for (const auto& root : m_rootObjects)
        advanceEnd(root.get());
}

void Scene::start() {
    // V0.3-1：进入 Play 时物理全量重建——所有 body 从当前 Transform 重建（覆盖 Edit 态
    // 的增量注册漂移，保证 Play 起始状态与编辑结果一致）
    if (m_physics) m_physics->syncFromTransforms();

    // 进入 Play 时调用一次：深度优先广播 onStart（顺序与 onUpdate 一致）。
    // 跳过非激活子树与 enabled=false 的 Behaviour（裁定 #6）。
    std::function<void(GameObject*)> broadcast = [&](GameObject* go) {
        if (go->activeSelf()) {
            for (auto& comp : go->m_components) {
                if (auto* b = dynamic_cast<Behaviour*>(comp.get()); b != nullptr && !b->enabled)
                    continue;
                comp->onStart();
            }
            for (auto& child : go->m_children)
                broadcast(child.get());
        }
    };
    for (const auto& root : m_rootObjects)
        broadcast(root.get());
}

}  // namespace NovaEngine
