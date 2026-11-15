#pragma once
#include <cstdint>
#include <functional>
#include <typeindex>

namespace NovaEngine {

// 事件系统（V0.3-11）：类型化事件 + 同步/延迟投递；publish 拷贝 handler 快照防重入；
// flush 用 swap 模式（flush 期间 post 进下帧队列，无重入）
class Events {
public:
    using Subscription = uint64_t;   // 0 = 无效；generation+counter 防悬挂

    template<typename E>
    static Subscription subscribe(const std::function<void(const E&)>& handler) {
        return subscribeImpl(std::type_index(typeid(E)),
            [handler](const void* p) { handler(*static_cast<const E*>(p)); });
    }
    template<typename E>
    static void publish(const E& event) {   // 同步：立即投递当前订阅者
        publishImpl(std::type_index(typeid(E)), &event);
    }
    template<typename E>
    static void post(const E& event) {      // 延迟：入队，帧末 flushDeferred 投递
        postImpl(std::type_index(typeid(E)), &event, sizeof(E),
                 [](const void* src, void* dst) { new (dst) E(*static_cast<const E*>(src)); },
                 [](void* p) { static_cast<E*>(p)->~E(); });
    }

    static void unsubscribe(Subscription sub);
    static void flushDeferred();                       // Scene::update 末尾（§15 第 e 步）
    static uint64_t lastFrameDeliveredCount();         // 验收统计

private:
    using Dispatcher = std::function<void(const void*)>;
    static Subscription subscribeImpl(std::type_index type, Dispatcher&& dispatcher);
    static void publishImpl(std::type_index type, const void* event);
    static void postImpl(std::type_index type, const void* src, size_t size,
                         const std::function<void(const void*, void*)>& copyCtor,
                         const std::function<void(void*)>& dtor);
};

}  // namespace NovaEngine
