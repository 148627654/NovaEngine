#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Core/UUID.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <algorithm>
#include <functional>

namespace NovaEngine {

namespace {
// 深度优先收集子树全部对象（用于 flushDestroyQueue 清理索引）
void collectSubtree(GameObject* root, std::vector<GameObject*>& out) {
    out.push_back(root);
    for (auto& child : root->children())
        collectSubtree(child.get(), out);
}
}  // namespace

Scene::Scene(const std::string& name) : m_name(name) {}

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

    if (parent != nullptr && parent->m_scene == this)
        parent->m_children.push_back(std::move(go));
    else {
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
    // Play 态：flush 销毁队列 + 深度优先广播 onUpdate
    flushDestroyQueue();

    std::function<void(GameObject*)> broadcast = [&](GameObject* go) {
        if (go->activeSelf()) {
            for (auto& comp : go->m_components) {
                // 裁定：Behaviour 的 enabled=false 时跳过 onUpdate
                if (auto* b = dynamic_cast<Behaviour*>(comp.get()); b != nullptr && !b->enabled)
                    continue;
                comp->onUpdate(dt);
            }
            for (auto& child : go->m_children)
                broadcast(child.get());
        }
        // 非激活对象整棵子树跳过（等价 activeInHierarchy 语义）
    };
    for (const auto& root : m_rootObjects)
        broadcast(root.get());
}

void Scene::start() {
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
