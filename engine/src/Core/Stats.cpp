// Stats.cpp —— 帧统计（V0.2-11）
//
// 全静态；自身用 steady_clock 计时（独立于 Time 的 0.1s 钳制）。
// 帧时间进 120 帧环形缓冲（游标写入），平均/尖峰按已填充窗口结算；
// frameTimeHistory 返回「最旧 → 最新」的线性视图（指针稳定，供直方图直接遍历）。

#include <NovaEngine/Core/Stats.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>

namespace NovaEngine {

namespace {

std::chrono::steady_clock::time_point g_t0{};

float g_lastFrameMs = 0.0f;
float g_averageMs = 0.0f;
float g_peakMs = 0.0f;

// 120 帧环形缓冲 + 游标（下一帧写入位置）
std::array<float, Stats::kHistoryFrames> g_ring{};
uint32_t g_cursor = 0;
uint32_t g_filled = 0;   // 已填充帧数（< kHistoryFrames 时 UI 只画前段）

// 读取视图：最旧 → 最新 的线性副本（frameTimeHistory 返回，指针稳定）
std::array<float, Stats::kHistoryFrames> g_historyView{};

// 命名分段耗时（固定三段；从未上报返回 -1.0f）
float g_sectionUpdate = -1.0f;
float g_sectionRender = -1.0f;
float g_sectionUi = -1.0f;

}  // namespace

void Stats::beginFrame() {
    g_t0 = std::chrono::steady_clock::now();
}

void Stats::endFrame() {
    const float ms = std::chrono::duration<float, std::milli>(
                         std::chrono::steady_clock::now() - g_t0)
                         .count();
    g_lastFrameMs = ms;

    // 写入环形缓冲
    g_ring[g_cursor] = ms;
    g_cursor = (g_cursor + 1) % kHistoryFrames;
    g_filled = std::min(g_filled + 1, kHistoryFrames);

    // 重建线性视图（最旧 → 最新），并结算均值/尖峰
    for (uint32_t i = 0; i < g_filled; ++i)
        g_historyView[i] = g_ring[(g_cursor + kHistoryFrames - g_filled + i) % kHistoryFrames];
    float sum = 0.0f;
    for (uint32_t i = 0; i < g_filled; ++i)
        sum += g_historyView[i];
    g_averageMs = g_filled > 0 ? sum / static_cast<float>(g_filled) : 0.0f;
    g_peakMs = g_filled > 0
                   ? *std::max_element(g_historyView.begin(), g_historyView.begin() + g_filled)
                   : 0.0f;
}

void Stats::setSectionMs(const char* name, float ms) {
    if (!name) return;
    const std::string_view section(name);
    if (section == "update") g_sectionUpdate = ms;
    else if (section == "render") g_sectionRender = ms;
    else if (section == "ui") g_sectionUi = ms;
    // 未知分段名：忽略（V0.2 固定三段，简单实现）
}

float Stats::lastFrameMs() { return g_lastFrameMs; }
float Stats::averageFrameMs() { return g_averageMs; }
float Stats::peakFrameMs() { return g_peakMs; }

float Stats::sectionMs(const char* name) {
    if (!name) return -1.0f;
    const std::string_view section(name);
    if (section == "update") return g_sectionUpdate;
    if (section == "render") return g_sectionRender;
    if (section == "ui") return g_sectionUi;
    return -1.0f;
}

const float* Stats::frameTimeHistory() { return g_historyView.data(); }
uint32_t Stats::historyCount() { return g_filled; }

void Stats::resetHistory() {
    g_cursor = 0;
    g_filled = 0;
    g_ring.fill(0.0f);
    g_historyView.fill(0.0f);
    g_averageMs = 0.0f;
    g_peakMs = 0.0f;
    // lastFrameMs 与分段值保留（当前帧仍有效；Clear 只清直方图）
}

}  // namespace NovaEngine
