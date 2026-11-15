// Behaviour.cpp —— 脚本组件基类实现（V0.3-8 协程 + V0.3-11 事件订阅自动清理）
//
// 协程推进协议（§15 帧内顺序；由 Scene::update 调用本方法，本文件不负责调用）：
//   advanceCoroutines(dt, false) → Events::flushDeferred() → advanceCoroutines(dt, true)
//   pass1：NextFrame/AfterSeconds/AfterCoroutine/Condition 放行；
//   pass2：EndOfFrame 放行 + 帧末统一 destroy+erase 全部 done 协程。
// 嵌套安全（§19.2）：
//   - std::list 节点地址稳定（WaitForCoroutine 持有裸指针）；
//   - resume 中 startCoroutine 追加的新协程本轮不处理（遍历终点快照）；
//   - resume 中 stopAllCoroutines 走「挂起请求」路径——不能 destroy 执行中的协程帧
//     （UB），advance 在全部 resume 返回（执行中协程已重新挂起）后统一销毁；
//   - done 协程回收前先唤醒等待者（防 waitFor 悬垂），唤醒可能连锁（工作列表处理）。
// 已知限制（冻结设计取舍）：WaitForCoroutine 仅支持同 Behaviour 内协程互等——
//   跨 Behaviour 等待时，目标列表的帧末回收会使 waitFor 悬垂（文档化，勿用）。

#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Core/Logger.h>

#include <unordered_set>
#include <vector>

namespace NovaEngine {

namespace {

// 正在推进协程的 Behaviour（单线程帧内唯一）；stopAllCoroutines 自调用检测用
Behaviour* s_advancingBehaviour = nullptr;
bool s_pendingStopAll = false;

// 链式唤醒上限（P1-1 修复）：waiter 在回收 sweep 内连续 co_await 已销毁目标的次数上限；
// 超限视为病态自旋（如 while(true) co_await WaitForCoroutine{已销毁}），销毁其帧防死循环
constexpr int kMaxChainResumes = 64;

}  // namespace

Behaviour::Behaviour(GameObject* owner) : Component(owner) {}

Behaviour::~Behaviour() {
    // 析构：协程全 destroy + 事件订阅全清（冻结头契约「销毁自动终止」）。
    // 析构发生在 Scene::flushDestroyQueue（Scene::update 第一步，§15），早于任何
    // advance——此时本组件无协程在执行，走立即销毁路径。
    stopAllCoroutines();
    for (const Events::Subscription sub : m_eventSubs)
        Events::unsubscribe(sub);
    m_eventSubs.clear();
}

Coroutine* Behaviour::startCoroutine(Coroutine&& routine) {
    if (!routine.handle())
        return nullptr;   // 空协程（默认构造）拒绝
    m_coroutines.push_back(std::move(routine));
    return &m_coroutines.back();   // list 节点地址稳定：WaitForCoroutine 依赖（§19.2）
}

void Behaviour::stopAllCoroutines() {
    if (this == s_advancingBehaviour) {
        // 在自身协程内调用（当前协程正在执行、帧挂在 resume 调用栈上）：
        // 立即 destroy 执行中的协程帧是 UB——挂起请求，advanceCoroutines
        // 在全部 resume 返回（执行中协程已重新挂起）后统一 destroy+clear
        s_pendingStopAll = true;
        return;
    }
    // 立即路径：本组件无协程在执行（外部调用 / 析构）
    for (auto& c : m_coroutines)
        c.destroy();
    m_coroutines.clear();
}

void Behaviour::advanceCoroutines(float dt, bool endOfFrame) {
    s_advancingBehaviour = this;
    s_pendingStopAll = false;

    // 遍历终点快照：resume 中 startCoroutine 追加的新协程本轮不处理（下轮处理，Unity 语义）
    const auto endIt = m_coroutines.end();
    for (auto it = m_coroutines.begin(); it != endIt && !s_pendingStopAll; ++it) {
        Coroutine& c = *it;
        if (c.done())
            continue;   // 已完成：帧末（endOfFrame 遍）统一回收

        auto& p = c.handle().promise();
        bool resumeNow = false;
        switch (p.resumeMode) {
        case Coroutine::promise_type::Resume::NextFrame:
            resumeNow = true;
            break;
        case Coroutine::promise_type::Resume::AfterSeconds:
            p.waitSeconds -= dt;
            resumeNow = p.waitSeconds <= 0.0f;
            break;
        case Coroutine::promise_type::Resume::EndOfFrame:
            resumeNow = endOfFrame;
            break;
        case Coroutine::promise_type::Resume::AfterCoroutine:
            // 等待目标已完成 → 放行。同 Behaviour 互等时目标回收晚于本轮检查，指针有效
            resumeNow = (p.waitFor != nullptr) && p.waitFor->done();
            break;
        case Coroutine::promise_type::Resume::Condition: {
            try {
                resumeNow = p.waitUntil && p.waitUntil();
            } catch (...) {
                Logger::error("协程条件谓词抛出异常，按未满足处理");
                resumeNow = false;
            }
            break;
        }
        }
        if (resumeNow)
            c.resume();   // 可能结束（done）或再次 co_await（resumeMode 已被新 await_transform 改写）
    }

    // stopAll 请求落地：此刻全部 resume 已返回（执行中的协程已重新挂起），destroy 安全；
    // 整表清空（同 Behaviour 等待者一并销毁，无悬垂）
    auto destroyAll = [&] {
        for (auto& c : m_coroutines)
            c.destroy();
        m_coroutines.clear();
    };

    if (s_pendingStopAll) {
        destroyAll();
    } else if (endOfFrame) {
        // 帧末统一 destroy+erase 全部 done 协程；先唤醒等待者再回收（防 waitFor 悬垂）。
        // P1-1 修复：FIFO 工作列表 + 「本 pass 已销毁目标」集合——唤醒后的 waiter 可能
        // co_await 一个本 pass 已销毁（或正在销毁）的协程，waitFor 立即悬垂成 UAF；
        // 链式唤醒检测到即续推，保证 sweep 结束时不存在指向已销毁节点的 waitFor。
        // P2-1 修复：回收循环内每次唤醒后复查 stopAll 请求（resume 中调用不得被吞）。
        std::vector<std::list<Coroutine>::iterator> pending;
        for (auto it = m_coroutines.begin(); it != m_coroutines.end(); ++it)
            if (it->done())
                pending.push_back(it);

        std::unordered_set<const Coroutine*> destroyed;   // 本 pass 已 destroy+erase 的目标地址
        size_t cursor = 0;                                // FIFO：链式唤醒新产生的 done 按序处理

        // 链式唤醒：resume 后 waiter 可能完成（入工作列表）或再次 co_await——若新 waitFor
        // 指向已销毁/正在销毁的目标则立即续推（目标已完成，等待无意义且指针悬垂）。
        // 注：startCoroutine 可能复用刚被 erase 释放的节点地址，导致 destroyed 集合误判
        //（极端罕见）——后果仅为该 waiter 被提前放行一次，无崩溃，可接受。
        auto resumeWaiter = [&](std::list<Coroutine>::iterator wit, const Coroutine* target) {
            for (int chain = 0; chain < kMaxChainResumes; ++chain) {
                wit->resume();
                if (wit->done()) {
                    pending.push_back(wit);
                    return;
                }
                auto& wp = wit->handle().promise();
                const bool waitsDead =
                    wp.resumeMode == Coroutine::promise_type::Resume::AfterCoroutine &&
                    wp.waitFor != nullptr &&
                    (destroyed.contains(wp.waitFor) || wp.waitFor == target);
                if (!waitsDead)
                    return;   // 等到存活目标或其它模式：正常挂起，后续帧/本 pass 稍后处理
            }
            // 病态自旋：协程反复等待已销毁目标（永不前进）——销毁其帧防死循环
            Logger::warn("协程链式唤醒超限（反复等待已销毁协程），已强制终止");
            wit->destroy();
            pending.push_back(wit);
        };

        while (cursor < pending.size()) {
            if (s_pendingStopAll) {   // P2-1：唤醒中 stopAll 请求 → 整表销毁并退出回收
                destroyAll();
                break;
            }
            auto it = pending[cursor++];
            Coroutine* target = &*it;
            // 唤醒正在等待本协程的等待者（同一帧放行）
            for (auto wit = m_coroutines.begin(); wit != m_coroutines.end(); ++wit) {
                if (wit == it || wit->done())
                    continue;
                auto& wp = wit->handle().promise();
                if (wp.resumeMode == Coroutine::promise_type::Resume::AfterCoroutine &&
                    wp.waitFor == target) {
                    resumeWaiter(wit, target);
                    if (s_pendingStopAll)
                        break;   // P2-1：立即交由外层整表销毁
                }
            }
            it->destroy();
            m_coroutines.erase(it);   // list erase 仅失效被删迭代器，pending 中其余保持有效
            destroyed.insert(target);
        }
    }

    s_advancingBehaviour = nullptr;
}

}  // namespace NovaEngine
