#pragma once
#include <coroutine>
#include <functional>
#include <NovaEngine/Core/Logger.h>

namespace NovaEngine {

// 协程 yield 原语（V0.3-8）
struct WaitForSeconds { float seconds; };
struct WaitForEndOfFrame {};
struct WaitUntil { std::function<bool()> predicate; };
class Coroutine;
struct WaitForCoroutine { const Coroutine* other; };   // 嵌套：等待另一协程结束

// 最小任务类型：promise 记录 yield 原因；帧推进由 Behaviour::advanceCoroutines 驱动。
// final_suspend=suspend_always + 手动 destroy（无 RAII 双删）；WaitForCoroutine 依赖 list 地址稳定。
class Coroutine {
public:
    struct promise_type {
        enum class Resume { NextFrame, AfterSeconds, EndOfFrame, AfterCoroutine, Condition };
        Resume resumeMode = Resume::NextFrame;
        float waitSeconds = 0.0f;
        const Coroutine* waitFor = nullptr;
        std::function<bool()> waitUntil;

        Coroutine get_return_object() { return Coroutine{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { Logger::error("协程未捕获异常，已终止"); }
        std::suspend_always await_transform(WaitForSeconds v) {
            resumeMode = Resume::AfterSeconds; waitSeconds = v.seconds; return {};
        }
        std::suspend_always await_transform(WaitForEndOfFrame) {
            resumeMode = Resume::EndOfFrame; return {};
        }
        std::suspend_always await_transform(WaitUntil v) {
            resumeMode = Resume::Condition; waitUntil = std::move(v.predicate); return {};
        }
        std::suspend_always await_transform(WaitForCoroutine v) {
            resumeMode = Resume::AfterCoroutine; waitFor = v.other; return {};
        }
    };

    Coroutine() = default;
    explicit Coroutine(std::coroutine_handle<promise_type> h) : m_handle(h) {}
    Coroutine(const Coroutine&) = delete;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine(Coroutine&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }
    Coroutine& operator=(Coroutine&& other) noexcept {
        if (this != &other) { if (m_handle) m_handle.destroy(); m_handle = other.m_handle; other.m_handle = nullptr; }
        return *this;
    }
    ~Coroutine() { if (m_handle) m_handle.destroy(); }

    bool done() const { return !m_handle || m_handle.done(); }
    void resume() { if (m_handle && !m_handle.done()) m_handle.resume(); }
    void destroy() { if (m_handle) { m_handle.destroy(); m_handle = nullptr; } }
    std::coroutine_handle<promise_type> handle() const { return m_handle; }

private:
    std::coroutine_handle<promise_type> m_handle = nullptr;
};

}  // namespace NovaEngine
