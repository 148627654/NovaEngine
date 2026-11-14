#pragma once
#include <atomic>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace NovaEngine {

class GameObject;
class Transform;

class Component {
public:
    explicit Component(GameObject* owner);
    virtual ~Component() = default;

    GameObject* gameObject() const;
    Transform* transform() const;     // owner->getComponent<Transform>() 便捷

    // 组件运行期类型 ID（进程内唯一，不持久化；持久化用类型名字符串）
    // public：SceneSerializer 反序列化挂组件时也需要计算 typeId
    template<typename T>
    static uint32_t componentTypeId() {
        static uint32_t id = s_nextTypeId.fetch_add(1);
        return id;
    }
    uint32_t typeId() const;

    // 生命周期（默认空实现）
    virtual void onAwake() {}           // 挂载后调用（attach 时；反序列化路径延迟到字段就绪后）
    virtual void onStart() {}           // 进入 Play 时调用一次
    virtual void onUpdate(float /*dt*/) {}  // Play 每帧调用

    // 序列化扩展字段（公共字段由 SceneSerializer 处理，子类只写自己独有的）
    virtual void onSerialize(nlohmann::json& /*out*/) const {}
    virtual void onDeserialize(const nlohmann::json& /*in*/) {}

private:
    GameObject* m_owner = nullptr;
    uint32_t m_typeId = 0;
    inline static std::atomic<uint32_t> s_nextTypeId{1};
    friend class GameObject;
};

}  // namespace NovaEngine
