#include <NovaEngine/Core/Time.h>

#include <algorithm>
#include <chrono>

namespace NovaEngine {

namespace {

std::chrono::steady_clock::time_point g_lastFrame{};
float g_deltaTime = 0.0f;
float g_totalTime = 0.0f;
uint64_t g_frameCount = 0;
bool g_initialized = false;

}  // namespace

void Time::beginFrame() {
    const auto now = std::chrono::steady_clock::now();
    if (!g_initialized) {
        g_lastFrame = now;
        g_initialized = true;
    }
    const float rawDt = std::chrono::duration<float>(now - g_lastFrame).count();
    g_lastFrame = now;

    // deltaTime 上限钳制 0.1s：断点调试 / 加载卡顿后避免脚本与相机大幅跳变
    g_deltaTime = std::min(rawDt, 0.1f);
    g_totalTime += rawDt;   // 总时间用未钳制值：语义为「启动以来秒数」
    ++g_frameCount;
}

float Time::deltaTime() { return g_deltaTime; }
float Time::totalTime() { return g_totalTime; }
uint64_t Time::frameCount() { return g_frameCount; }

}  // namespace NovaEngine
