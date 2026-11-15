#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "Component.h"

namespace NovaEngine {

class Scene;
class Transform;

class GameObject {
public:
    GameObject(uint64_t id, const std::string& name);
    ~GameObject();
    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;

    uint64_t id() const;
    const std::string& name() const;
    void setName(const std::string& name);
    bool activeSelf() const;
    void setActiveSelf(bool active);
    bool activeInHierarchy() const;     // 自身 && 所有祖先 activeSelf

    // 层级
    GameObject* parent() const;
    void setParent(GameObject* newParent, bool keepWorldTransform = true);
    const std::vector<std::unique_ptr<GameObject>>& children() const;

    // 组件
    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {   // 同类型已存在则 assert
        static_assert(std::is_base_of_v<Component, T>);
        auto comp = std::make_unique<T>(this, std::forward<Args>(args)...);
        const uint32_t typeId = Component::componentTypeId<T>();
        comp->m_typeId = typeId;
        T* ptr = comp.get();
        attachComponent(std::move(comp), typeId);
        return ptr;
    }
    // 反序列化用：由调用方给出 typeId（ComponentRegistry::typeIdForName），callAwake=false 时延迟 onAwake 到字段就绪后
    void attachComponent(std::unique_ptr<Component> comp, uint32_t typeId, bool callAwake = true);

    template<typename T>
    T* getComponent() {                 // 按 typeId 线性扫描
        for (auto& c : m_components)
            if (c->m_typeId == Component::componentTypeId<T>())
                return static_cast<T*>(c.get());
        return nullptr;
    }
    template<typename T>
    const T* getComponent() const {     // const 版本（transform() const 等 const 路径使用）
        for (const auto& c : m_components)
            if (c->m_typeId == Component::componentTypeId<T>())
                return static_cast<const T*>(c.get());
        return nullptr;
    }
    template<typename T>
    bool hasComponent() { return getComponent<T>() != nullptr; }
    template<typename T>
    void removeComponent() {
        std::erase_if(m_components, [](const std::unique_ptr<Component>& c) {
            return c->m_typeId == Component::componentTypeId<T>();
        });
    }
    template<typename T>
    std::vector<T*> getComponents() {   // 含派生类
        std::vector<T*> out;
        for (auto& c : m_components)
            if (auto* p = dynamic_cast<T*>(c.get()))
                out.push_back(p);
        return out;
    }
    Transform* transform() const;

    Scene* scene() const;
    void destroy();     // 标记销毁（含子树），Scene 帧末真正删除

private:
    uint64_t m_id = 0;
    std::string m_name;
    bool m_activeSelf = true;
    GameObject* m_parent = nullptr;
    std::vector<std::unique_ptr<GameObject>> m_children;
    std::vector<std::unique_ptr<Component>> m_components;
    Scene* m_scene = nullptr;
    friend class Scene;
};

}  // namespace NovaEngine
