#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <NovaEngine/Scene/Component.h>

namespace NovaEngine {

class GameObject;

// 内置组件名 → 工厂注册表（V0.3 波 0）：SceneSerializer 组件分发的唯一入口；
// 各组件 .cpp 末尾用 NE_REGISTER_COMPONENT 自注册（幂等；名字含 "::" 取最后一段注册）
class ComponentRegistry {
public:
    using Factory = std::function<std::unique_ptr<Component>(GameObject*)>;
    // 幂等；名字含 "::" 取最后一段注册；typeId 由宏侧 Component::componentTypeId<ClassName>() 提供
    static void registerBuiltin(const std::string& typeName, uint32_t typeId, Factory factory);
    static std::unique_ptr<Component> create(const std::string& typeName, GameObject* owner);  // 未知返回 nullptr
    static uint32_t typeIdForName(const std::string& typeName);   // 未知返回 0（序列化 attach 用）
    static bool exists(const std::string& typeName);
    static std::vector<std::string> builtinNames();   // Inspector Add Component 下拉（排序）
private:
    struct Entry { uint32_t typeId; Factory factory; };
    static std::unordered_map<std::string, Entry>& entries();  // 函数内静态防初始化顺序问题
};

}  // namespace NovaEngine

// 组件 .cpp 文件末尾放置（注册宏；匿名命名空间 + 静态注册结构，跨翻译单元天然不冲突）
#define NE_REGISTER_COMPONENT(ClassName) \
    namespace { \
    struct NE_ComponentRegistrar { \
        NE_ComponentRegistrar() { \
            ::NovaEngine::ComponentRegistry::registerBuiltin(#ClassName, \
                ::NovaEngine::Component::componentTypeId<ClassName>(), \
                [](::NovaEngine::GameObject* go) { return std::make_unique<ClassName>(go); }); \
        } \
    }; \
    const NE_ComponentRegistrar NE_component_registrar_instance; \
    }
