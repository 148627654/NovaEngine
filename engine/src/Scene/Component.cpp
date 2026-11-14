#include <NovaEngine/Scene/Component.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>

namespace NovaEngine {

Component::Component(GameObject* owner) : m_owner(owner) {}

GameObject* Component::gameObject() const {
    return m_owner;
}

Transform* Component::transform() const {
    // owner->getComponent<Transform>() 便捷访问
    return m_owner ? m_owner->getComponent<Transform>() : nullptr;
}

uint32_t Component::typeId() const {
    return m_typeId;
}

}  // namespace NovaEngine
