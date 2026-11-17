#include "StatsPanel.h"

#include "../EditorContext.h"

#include <imgui.h>

#include <NovaEngine/Core/Stats.h>
#include <NovaEngine/Render/Renderer.h>

#include <algorithm>
#include <cstdio>

namespace NovaEditor {

StatsPanel::StatsPanel() : Panel("Stats") {
    open = false;  // 默认关，F8 开关（V0.2-11）
}

void StatsPanel::onGui(EditorContext&, EditorCamera&) {
    using namespace NovaEngine;
    ImGui::Begin(m_name.c_str(), &open);

    if (ImGui::Button("Clear")) Stats::resetHistory();
    ImGui::Separator();

    // FPS 用滚动均值（1000/avg），避免单帧抖动
    const float avg = Stats::averageFrameMs();
    const float fps = avg > 0.0f ? 1000.0f / avg : 0.0f;
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame: avg %.2f ms | peak %.2f ms | last %.2f ms", avg,
                Stats::peakFrameMs(), Stats::lastFrameMs());
    ImGui::Separator();

    // 三段耗时（首次未上报为 -1.0，属预热帧）
    ImGui::Text("Update %.2f ms | Render %.2f ms | UI %.2f ms",
                Stats::sectionMs("update"), Stats::sectionMs("render"),
                Stats::sectionMs("ui"));
    ImGui::Separator();

    // 渲染计数器（上一帧结算值）
    ImGui::Text("Draw calls: %u", Renderer::lastDrawCalls());
    ImGui::Text("Submitted: %u", Renderer::lastSubmittedCount());
    ImGui::Text("Triangles: %u", Renderer::lastTriangleCount());
    ImGui::Text("Shadow items: %u", Renderer::lastShadowItemCount());
    // 剔除/实例化统计（V0.5-14：上一帧结算值；剔除关闭时 Culled 恒 0）
    ImGui::Text("Culled: %u", Renderer::lastCulledCount());
    ImGui::Text("Instanced draws: %u", Renderer::lastInstancedDrawCalls());
    ImGui::Text("Instances: %u", Renderer::lastInstancedInstanceCount());
    ImGui::Separator();

    // 120 帧历史直方图：historyCount 截断；纵轴 max(peak, 1.0) 防除零；高 80px
    const uint32_t count = Stats::historyCount();
    const float* history = Stats::frameTimeHistory();
    if (count > 0 && history) {
        const float scaleMax = std::max(Stats::peakFrameMs(), 1.0f);
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%.2f ms", Stats::lastFrameMs());
        ImGui::PlotHistogram("##frame_history", history, static_cast<int>(count), 0, overlay,
                             0.0f, scaleMax, ImVec2(0.0f, 80.0f));
    } else {
        ImGui::TextUnformatted("(no frame history yet)");
    }

    ImGui::End();
}

}  // namespace NovaEditor
