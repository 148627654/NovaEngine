#pragma once
#include <NovaEngine/Scene/Component.h>
#include <NovaEngine/Scene/GameObject.h>
#include <type_traits>

namespace NovaEngine {

class Behaviour : public Component {
public:
    explicit Behaviour(GameObject* owner);

    bool enabled = true;
    // onStart/onUpdate 继承自 Component，子类直接 override
};

// 脚本便捷查找（裁定 #4）：所有脚本组件共用 Behaviour 的 typeId，
// 按具体类型用 dynamic_cast 过滤；getComponent<具体脚本T>() 不保证命中。
template<typename T>
T* findScript(GameObject* go) {
    static_assert(std::is_base_of_v<Behaviour, T>);
    if (go == nullptr)
        return nullptr;
    for (auto* c : go->getComponents<Behaviour>())
        if (auto* s = dynamic_cast<T*>(c))
            return s;
    return nullptr;
}

}  // namespace NovaEngine
