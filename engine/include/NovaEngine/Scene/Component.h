#pragma once
#include <cstdint>
#include <typeinfo>
#include <nlohmann/json_fwd.hpp>

namespace NovaEngine {

class GameObject;
class Transform;

// FNV-1a 32 位（组件 typeId 派生用；见 Component::componentTypeId）
inline uint32_t componentNameHash(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= static_cast<unsigned char>(*s++);
        h *= 16777619u;
    }
    return h;
}

class Component {
public:
    explicit Component(GameObject* owner);
    virtual ~Component() = default;

    GameObject* gameObject() const;
    Transform* transform() const;     // owner->getComponent<Transform>() 便捷

    // 组件运行期类型 ID（进程内唯一，不持久化；持久化用类型名字符串）
    // public：SceneSerializer 反序列化挂组件时也需要计算 typeId
    // 2026-11-23 跨 DLL 修复（CharacterWalk 踩雷）：id 由类型名 FNV-1a 哈希派生——
    // 旧实现按模块内静态计数器 fetch_add，exe 与脚本 DLL 各持一份计数器，
    // DLL 侧 getComponent<T>() 算出的 id 与 exe 侧注册值不一致 → 恒空。
    // typeid 抹名串由编译器确定，两模块同名同类 → 同 id（与 type_info 是否共享无关）。
    template<typename T>
    static uint32_t componentTypeId() {
        static const uint32_t id = componentNameHash(typeid(T).name());
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
    friend class GameObject;
};

}  // namespace NovaEngine
