#pragma once
#include "../Panel.h"

#include <NovaEngine/Profiling/Profiler.h>

#include <cstdint>
#include <vector>

namespace NovaEditor {

// Profiler 面板（V1.0-1，cR 波 1）：帧时间线（600 帧环形折线，点击定位某帧）+
// 该帧 CPU 段横向堆叠条（火焰图简版）+ GPU 段 + 内存分类表。
// dock/菜单接线归合波日（主会话，§26.0）；本面板仅提供 Panel 形态 onGui 接口。
class ProfilerPanel : public Panel {
public:
    explicit ProfilerPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    // 选中帧号（Profiler::frame(index) 的环形定位帧号）；-1 = 跟随最新帧
    int64_t m_selectedFrame = -1;
    bool m_autoFollow = true;
    std::vector<float> m_timelineScratch;   // 时间线帧时长暂存（避免每帧重分配）

    // 选中帧号 → 环形 index（越界/未选中返回 -1）
    int resolveSelectedIndex(uint32_t count) const;
    // 帧时间线（返回选中帧记录；nullptr = 无选中）
    const NovaEngine::FrameRecord* drawTimeline();
    void drawCpuSections(const NovaEngine::FrameRecord& rec);
    void drawGpuSections(const NovaEngine::FrameRecord& rec);
    void drawMemory(const NovaEngine::MemorySnapshot& mem);
};

}  // namespace NovaEditor
