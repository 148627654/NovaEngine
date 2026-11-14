#pragma once
#include <cstdint>

namespace NovaEngine {

// 帧统计（V0.2-11）：帧时间滚动均值/尖峰 + 命名分段耗时 + 120 帧历史（直方图用）。
// 全静态、无依赖；自身用 steady_clock 计时（独立于 Time 的 0.1s 钳制）。
class Stats {
public:
    static constexpr uint32_t kHistoryFrames = 120;

    static void beginFrame();                      // 帧首（EditorApplication::run 内 Time::beginFrame 之后）
    static void endFrame();                        // 帧末（swapBuffers 前）：结算本帧耗时入环形历史
    static void setSectionMs(const char* name, float ms);   // 分段上报："update"/"render"/"ui"
    // 读取（Stats 面板用）
    static float lastFrameMs();
    static float averageFrameMs();                 // 最近 kHistoryFrames 均值
    static float peakFrameMs();                    // 最近 kHistoryFrames 最大值
    static float sectionMs(const char* name);      // 该段最近一帧耗时（从未上报返回 -1.0f）
    static const float* frameTimeHistory();        // kHistoryFrames 环形缓冲（最旧 → 最新）
    static uint32_t historyCount();                // 已填充帧数（< kHistoryFrames 时 UI 只画前段）
    static void resetHistory();                    // Stats 面板 Clear 按钮
};

}  // namespace NovaEngine
