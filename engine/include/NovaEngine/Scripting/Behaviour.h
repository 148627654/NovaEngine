#pragma once
#include <functional>
#include <list>
#include <vector>
#include <type_traits>
#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Scene/Component.h>
#include <NovaEngine/Scene/GameObject.h>
#include "Coroutine.h"

namespace NovaEngine {

class Behaviour : public Component {
public:
    explicit Behaviour(GameObject* owner);
    ~Behaviour() override;   // 析构：协程全 destroy + 事件订阅全清（销毁自动终止）

    bool enabled = true;
    // onStart/onUpdate 继承自 Component，子类直接 override

    // ===== V0.3-8 协程 =====
    Coroutine* startCoroutine(Coroutine&& routine);   // list 保地址稳定（WaitForCoroutine 依赖）
    void stopAllCoroutines();

    // ===== V0.3-11 事件订阅自动清理 =====
    template<typename E>
    void subscribeEvent(const std::function<void(const E&)>& handler) {
        m_eventSubs.push_back(Events::subscribe<E>(handler));
    }

private:
    std::list<Coroutine> m_coroutines;
    std::vector<Events::Subscription> m_eventSubs;
    void advanceCoroutines(float dt, bool endOfFrame);   // Scene::update 调用（§15 帧内顺序）
    friend class Scene;
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
