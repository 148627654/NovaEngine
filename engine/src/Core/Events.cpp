// Events.cpp —— 类型化事件系统（V0.3-11）
//
// 设计要点（§14.8 / 任务书 5）：
//  - type_index → 订阅表；Subscription = generation(高32位)+counter(低32位)，0 无效
//  - publish：先拷贝 handler 快照再逐个调用——handler 内 unsubscribe 只标记死条目，
//    不影响正在进行的本轮分发（分发结束后统一清死条目）
//  - post：入队时 heap 拷贝事件对象（copyCtor + 析构器），flushDeferred 用 swap 模式
//    ——flush 期间新 post 进下帧队列，无重入
//  - handler 异常吞掉记日志（Unity 风格：一个订阅者异常不拖垮广播）
//  - 单线程（引擎唯一合法非主线程是 miniaudio 设备线程，与事件系统无关），无锁
//  - s_states/s_deferred 为文件内静态：本文件是事件系统的唯一实现 TU，全部访问
//    都发生在运行期（静态初始化之后），无跨翻译单元初始化顺序问题

#include <NovaEngine/Core/Events.h>
#include <NovaEngine/Core/Logger.h>

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace NovaEngine {

namespace {

struct Subscriber {
    Events::Subscription id = 0;   // 0 = 已退订（死条目：发布时跳过，分发后清理）
    // 注意：Events::Dispatcher 是私有别名（类外不可见），此处用原始类型等价替代
    std::function<void(const void*)> dispatcher;
};

struct TypeState {
    uint32_t generation = 1;   // 每次退订/清死条目自增：防 counter 回绕撞号
    uint32_t counter = 1;      // 本代内的句柄序号
    std::vector<Subscriber> subs;
};

// 延迟事件条目：事件对象已在堆上拷贝完成（postImpl 内 copyCtor）
struct DeferredEvent {
    std::type_index type;
    std::unique_ptr<std::byte[]> storage;   // 分配 size 字节（默认 new 按 max_align 对齐）
    std::function<void(void*)> dtor;        // 析构 storage 内的事件对象
    // DevOps 修复：std::type_index 无默认构造（C++26 才引入），隐式默认构造被删除，
    // 显式构造并在 postImpl 调用点传入类型
    explicit DeferredEvent(std::type_index t) : type(t) {}
};

std::unordered_map<std::type_index, TypeState> s_states;
std::vector<DeferredEvent> s_deferred;
uint64_t s_lastDelivered = 0;   // 最近一次 flushDeferred 的投递次数（验收统计）

TypeState& stateFor(std::type_index type) {
    return s_states[type];
}

}  // namespace

Events::Subscription Events::subscribeImpl(std::type_index type, Dispatcher&& dispatcher) {
    TypeState& st = stateFor(type);
    // generation+counter 组合唯一（0 保留无效；counter 回绕由 generation 自增兜底）
    const Subscription id = (static_cast<uint64_t>(st.generation) << 32) | st.counter++;
    Subscriber s;
    s.id = id;
    s.dispatcher = std::move(dispatcher);
    st.subs.push_back(std::move(s));
    return id;
}

void Events::unsubscribe(Subscription sub) {
    if (sub == 0)
        return;
    // 句柄不含类型信息：全表线性扫描（mini 引擎类型/订阅规模下可忽略）。
    // 只标记死条目，不即时 erase——正在进行的 publish 快照不受影响，分发后统一清理。
    for (auto& kv : s_states) {
        TypeState& st = kv.second;
        for (Subscriber& s : st.subs) {
            if (s.id == sub) {
                s.id = 0;
                s.dispatcher = nullptr;   // 释放 handler 捕获的资源（如 shared_ptr）
                st.generation++;          // 代际自增：其余句柄 id 组合不受影响，仅防撞号
                return;
            }
        }
    }
}

void Events::publishImpl(std::type_index type, const void* event) {
    TypeState& st = stateFor(type);

    // 快照：拷贝存活 handler（分发期间 unsubscribe 只标记死条目，快照副本仍有效；
    // 新订阅/退订都不影响本轮已定快照）
    std::vector<Dispatcher> snapshot;
    snapshot.reserve(st.subs.size());
    for (const Subscriber& s : st.subs)
        if (s.id != 0)
            snapshot.push_back(s.dispatcher);

    for (const Dispatcher& d : snapshot) {
        try {
            d(event);
        } catch (const std::exception& ex) {
            Logger::error("事件处理器抛出异常: {}", ex.what());
        } catch (...) {
            Logger::error("事件处理器抛出未知异常");
        }
    }

    // 分发完毕统一清死条目（发布期间标记的退订在此落地）
    std::erase_if(st.subs, [](const Subscriber& s) { return s.id == 0; });
}

void Events::postImpl(std::type_index type, const void* src, size_t size,
                      const std::function<void(const void*, void*)>& copyCtor,
                      const std::function<void(void*)>& dtor) {
    // 入队即 heap 拷贝：调用方的事件对象在 post 返回后可能失效
    DeferredEvent e(type);   // DevOps 修复：经显式构造传入类型（type_index 无默认构造）
    e.storage = std::make_unique<std::byte[]>(size);
    copyCtor(src, e.storage.get());
    e.dtor = dtor;
    s_deferred.push_back(std::move(e));
}

void Events::flushDeferred() {
    s_lastDelivered = 0;
    if (s_deferred.empty())
        return;

    // swap 模式：flush 期间新 post 进（已清空的）主队列 → 下一帧投递，无重入（§14.8）
    std::vector<DeferredEvent> local;
    local.swap(s_deferred);

    for (DeferredEvent& e : local) {
        // 投递数统计（按投递时刻的存活订阅数计）
        const TypeState& st = stateFor(e.type);
        for (const Subscriber& s : st.subs)
            if (s.id != 0)
                ++s_lastDelivered;
        publishImpl(e.type, e.storage.get());
        e.dtor(e.storage.get());   // 事件对象析构（storage 内存随 unique_ptr 释放）
    }
}

uint64_t Events::lastFrameDeliveredCount() {
    return s_lastDelivered;
}

}  // namespace NovaEngine
