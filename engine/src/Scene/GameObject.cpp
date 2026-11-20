#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Core/Logger.h>
#include <algorithm>
#include <glm/gtx/matrix_decompose.hpp>

namespace NovaEngine {

GameObject::GameObject(uint64_t id, const std::string& name) : m_id(id), m_name(name) {}

GameObject::~GameObject() = default;

uint64_t GameObject::id() const {
    return m_id;
}

const std::string& GameObject::name() const {
    return m_name;
}

void GameObject::setName(const std::string& name) {
    m_name = name;
}

bool GameObject::activeSelf() const {
    return m_activeSelf;
}

void GameObject::setActiveSelf(bool active) {
    m_activeSelf = active;
}

bool GameObject::dontDestroyOnLoad() const {
    return m_dontDestroyOnLoad;
}

void GameObject::setDontDestroyOnLoad(bool value) {
    m_dontDestroyOnLoad = value;
}

bool GameObject::activeInHierarchy() const {
    // 自身 && 所有祖先 activeSelf
    for (const GameObject* p = this; p != nullptr; p = p->m_parent)
        if (!p->m_activeSelf)
            return false;
    return true;
}

GameObject* GameObject::parent() const {
    return m_parent;
}

void GameObject::setParent(GameObject* newParent, bool keepWorldTransform) {
    if (newParent == m_parent)
        return;  // 重复挂到同一父级：无操作

    // 循环防护：新父级不能是自身或自身的后代
    for (GameObject* p = newParent; p != nullptr; p = p->m_parent) {
        if (p == this) {
            Logger::warn("setParent 失败：不能把对象挂到自身或其后代之下");
            return;
        }
    }
    if (newParent != nullptr && newParent->m_scene != m_scene) {
        Logger::warn("setParent 失败：父子对象不属于同一场景");
        return;
    }

    // keepWorldTransform=true：改父前先取出当前世界矩阵，改父后用 glm::decompose 反解局部 TRS
    glm::mat4 world{1.0f};
    bool hasWorld = false;
    if (keepWorldTransform) {
        if (Transform* t = transform()) {
            world = t->worldMatrix();
            hasWorld = true;
        }
    }

    // 从旧父级（或场景根列表）摘除，转移 unique_ptr 所有权
    std::unique_ptr<GameObject> self;
    if (m_parent != nullptr) {
        auto& list = m_parent->m_children;
        auto it = std::find_if(list.begin(), list.end(),
                               [this](const std::unique_ptr<GameObject>& c) { return c.get() == this; });
        if (it != list.end()) {
            self = std::move(*it);
            list.erase(it);
        }
    } else if (m_scene != nullptr) {
        auto& list = m_scene->m_rootObjects;
        auto it = std::find_if(list.begin(), list.end(),
                               [this](const std::unique_ptr<GameObject>& c) { return c.get() == this; });
        if (it != list.end()) {
            self = std::move(*it);
            list.erase(it);
        }
    }
    if (!self) {
        Logger::warn("setParent：对象不在原父级的子列表中（内部状态异常），仅更新父指针");
        m_parent = newParent;
        return;
    }

    // 挂到新父级（或根列表）
    if (newParent != nullptr)
        newParent->m_children.push_back(std::move(self));
    else if (m_scene != nullptr)
        m_scene->m_rootObjects.push_back(std::move(self));
    m_parent = newParent;

    // keepWorldTransform=true：世界矩阵反解回局部 TRS
    // ===== P1 修复（2026-11-20 波 1 合波日，cT 实测报出）=====
    // 原实现直接 decompose 旧世界矩阵——仅新父级为恒等时世界位置保持成立；
    // 偏移父级下拖拽重父子会跳变（实测 keep 后 world (11,2,3) 跳变）。
    // 修法：先换算到新父级局部空间 local = inv(newParentWorld) * world 再 decompose
    //（新父级为场景根时局部 = 世界，decompose 路径不变）。
    if (hasWorld) {
        if (Transform* t = transform()) {
            glm::mat4 local = world;
            if (newParent != nullptr && newParent->transform() != nullptr)
                local = glm::inverse(newParent->transform()->worldMatrix()) * world;
            glm::vec3 scale{1.0f};
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 position{0.0f};
            glm::vec3 skew{0.0f};
            glm::vec4 perspective{0.0f};
            glm::decompose(local, scale, rotation, position, skew, perspective);
            t->setLocalPosition(position);
            t->setLocalRotation(rotation);
            t->setLocalScale(scale);
        }
    }

    // 两种情况（keepWorld 与否）世界矩阵都随父级改变 → 递归置脏自身与全部后代
    if (Transform* t = transform())
        t->markDirty();
}

const std::vector<std::unique_ptr<GameObject>>& GameObject::children() const {
    return m_children;
}

void GameObject::attachComponent(std::unique_ptr<Component> comp, uint32_t typeId, bool callAwake) {
    if (!comp)
        return;
    comp->m_owner = this;
    comp->m_typeId = typeId;
    m_components.push_back(std::move(comp));
    if (callAwake)
        m_components.back()->onAwake();  // 挂载后立即调用
}

Transform* GameObject::transform() const {
    // const 上下文中 getComponent<T>() 选中 const 重载，const_cast 回非 const 指针
    // （接口约定：transform() const 返回可修改的 Transform*）
    return const_cast<Transform*>(getComponent<Transform>());
}

Scene* GameObject::scene() const {
    return m_scene;
}

void GameObject::destroy() {
    // 标记销毁（含子树）：后代先入队、自身后入队，Scene::flushDestroyQueue 帧末真正删除。
    // 严禁在 forEachObject/update 回调内直接 delete——统一走延迟队列。
    if (m_scene == nullptr)
        return;
    for (auto& child : m_children)
        child->destroy();
    m_scene->destroyGameObject(m_id);
}

}  // namespace NovaEngine
