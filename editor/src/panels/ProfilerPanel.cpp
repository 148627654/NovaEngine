// ProfilerPanel.cpp —— Profiler 面板（V1.0-1，cR 波 1，§26.2）：
// 帧时间线（600 帧环形折线，点击定位某帧）+ 该帧 CPU 段横向堆叠条（火焰图简版）
// + GPU 段（未就绪/不支持显示 n/a）+ 内存分类表。数据全部来自 Profiler（Profiling/）。
#include "ProfilerPanel.h"

#include "../EditorContext.h"

#include <imgui.h>

#include <NovaEngine/Profiling/ProfilerSections.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace NovaEditor {

namespace {

using namespace NovaEngine;

// CPU 段配色（按段序取模；模块档与脚本档混排不冲突）
ImU32 sectionColor(int index) {
    static const ImU32 kPalette[] = {
        IM_COL32(0x4E, 0x79, 0xA7, 0xFF),   // 蓝
        IM_COL32(0xF2, 0x8E, 0x2B, 0xFF),   // 橙
        IM_COL32(0x59, 0xA1, 0x4F, 0xFF),   // 绿
        IM_COL32(0xE1, 0x57, 0x59, 0xFF),   // 红
        IM_COL32(0xB0, 0x7A, 0xA1, 0xFF),   // 紫
        IM_COL32(0x76, 0xB7, 0xB2, 0xFF),   // 青
        IM_COL32(0xED, 0xC9, 0x48, 0xFF),   // 黄
        IM_COL32(0xFF, 0x9D, 0xA7, 0xFF),   // 粉
    };
    return kPalette[index % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

const char* formatMs(float ms, char* buf, size_t size) {
    std::snprintf(buf, size, "%.2f ms", ms);
    return buf;
}

// 字节数 → 人类可读（B/KB/MB，1024 进制）
const char* formatBytes(size_t bytes, char* buf, size_t size) {
    if (bytes >= 1024 * 1024)
        std::snprintf(buf, size, "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        std::snprintf(buf, size, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(buf, size, "%zu B", bytes);
    return buf;
}

}  // namespace

ProfilerPanel::ProfilerPanel() : Panel("Profiler") {}

int ProfilerPanel::resolveSelectedIndex(uint32_t count) const {
    if (m_selectedFrame < 0 || count == 0) return -1;
    // 环形定位：index 0 = 最旧（帧号 = 最新帧号 - count + 1）
    // 最新帧号从 frame(count - 1) 读取（count > 0 已保证）
    const uint64_t newest = Profiler::frame(count - 1)->frameNumber;
    const uint64_t oldest = newest - count + 1;
    if (static_cast<uint64_t>(m_selectedFrame) < oldest) return -1;   // 已滚出缓冲区
    return static_cast<int>(static_cast<uint64_t>(m_selectedFrame) - oldest);
}

const FrameRecord* ProfilerPanel::drawTimeline() {
    const uint32_t count = Profiler::frameCount();
    if (count == 0) {
        ImGui::TextDisabled("（无帧数据：等待 Profiler 接线 beginFrame/endFrame）");
        return nullptr;
    }

    // 帧时长序列（0 = 最旧）
    m_timelineScratch.resize(count);
    float maxMs = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        m_timelineScratch[i] = Profiler::frame(i)->frameMs;
        maxMs = std::max(maxMs, m_timelineScratch[i]);
    }
    maxMs = std::max(maxMs, 1.0f);   // 防除零

    const float availW = ImGui::GetContentRegionAvail().x;
    const float chartH = 110.0f;
    ImGui::InvisibleButton("##timeline", ImVec2(availW, chartH));
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 背景与边框
    dl->AddRectFilled(p0, p1, IM_COL32(0x1A, 0x1A, 0x1E, 0xFF));
    dl->AddRect(p0, p1, IM_COL32(0x50, 0x50, 0x58, 0xFF));

    // 折线（x 均匀铺满；y = 帧时长）
    const float w = p1.x - p0.x, h = p1.y - p0.y;
    if (count >= 2) {
        for (uint32_t i = 0; i + 1 < count; ++i) {
            const float x0 = p0.x + w * static_cast<float>(i) / static_cast<float>(count - 1);
            const float x1 = p0.x + w * static_cast<float>(i + 1) / static_cast<float>(count - 1);
            const float y0 = p1.y - h * (m_timelineScratch[i] / maxMs);
            const float y1 = p1.y - h * (m_timelineScratch[i + 1] / maxMs);
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0x4E, 0xC9, 0xB0, 0xFF), 1.5f);
        }
    }

    // 点击定位帧：x → index（0 = 最旧）
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float t = std::clamp((ImGui::GetMousePos().x - p0.x) / w, 0.0f, 1.0f);
        const int index = static_cast<int>(t * static_cast<float>(count - 1) + 0.5f);
        m_selectedFrame = static_cast<int64_t>(Profiler::frame(index)->frameNumber);
        m_autoFollow = false;   // 手动点选后不再跟随
    }
    if (ImGui::IsItemHovered()) {
        const float t = std::clamp((ImGui::GetMousePos().x - p0.x) / w, 0.0f, 1.0f);
        const int index = static_cast<int>(t * static_cast<float>(count - 1) + 0.5f);
        const FrameRecord* rec = Profiler::frame(static_cast<uint32_t>(index));
        ImGui::SetTooltip("帧 %llu：%.2f ms", static_cast<unsigned long long>(rec->frameNumber),
                          rec->frameMs);
    }

    // 选中帧竖线 + 最新帧尾标
    int selIndex = resolveSelectedIndex(count);
    if (selIndex < 0 && m_autoFollow) selIndex = static_cast<int>(count) - 1;
    if (selIndex >= 0) {
        const float x = (count >= 2) ? p0.x + w * static_cast<float>(selIndex) /
                                                  static_cast<float>(count - 1)
                                     : p0.x;   // 单帧：标在最左
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(0xFF, 0xE0, 0x60, 0xFF), 1.5f);
    }

    const FrameRecord* selected = nullptr;
    if (selIndex >= 0) {
        selected = Profiler::frame(static_cast<uint32_t>(selIndex));
        ImGui::Text("帧 %llu | %.2f ms | 纵轴 %.2f ms",
                    static_cast<unsigned long long>(selected->frameNumber),
                    selected->frameMs, maxMs);
    }
    return selected;
}

void ProfilerPanel::drawCpuSections(const FrameRecord& rec) {
    // 收集本帧已上报段（未上报 = -1）
    struct Section { const char* name; float ms; int index; };
    std::vector<Section> sections;
    for (int i = 0; i < Profiler::kMaxSections; ++i) {
        if (rec.sectionMs[i] < 0.0f) continue;
        const char* name = ProfilerSections::sectionName(i);
        sections.push_back({name ? name : "(未知)", rec.sectionMs[i], i});
    }
    // 排序：耗时降序（验收：可见脚本名分段耗时排序）
    std::stable_sort(sections.begin(), sections.end(),
                     [](const Section& a, const Section& b) { return a.ms > b.ms; });

    float sumMs = 0.0f;
    for (const auto& s : sections) sumMs += s.ms;
    const float scale = std::max(rec.frameMs, sumMs);   // 堆叠条基准 = max(帧时长, 段总和)
    if (scale <= 0.0f) return;

    // 横向堆叠条（火焰图简版）
    const float availW = ImGui::GetContentRegionAvail().x;
    const float barH = 18.0f;
    ImGui::InvisibleButton("##cpu_bar", ImVec2(availW, barH));
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(0x1A, 0x1A, 0x1E, 0xFF));
    float x = p0.x;
    const float w = p1.x - p0.x;
    for (const auto& s : sections) {
        const float segW = w * (s.ms / scale);
        if (segW < 0.5f) continue;   // 过窄不画（图例仍列出）
        dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(x + segW, p1.y), sectionColor(s.index));
        x += segW;
    }
    dl->AddRect(p0, p1, IM_COL32(0x50, 0x50, 0x58, 0xFF));

    // 图例列表（耗时降序 + 色块 + 占比）
    if (ImGui::BeginTable("##cpu_sections", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("段名", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("耗时", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("占比", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();
        for (const auto& s : sections) {
            ImGui::TableNextRow();
            ImGui::PushID(s.index);   // 行内控件 ID 隔离（表行无隐式 ID）
            ImGui::TableNextColumn();
            ImGui::ColorButton("##sw", ImGui::ColorConvertU32ToFloat4(sectionColor(s.index)),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
            ImGui::SameLine();
            ImGui::TextUnformatted(s.name);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f ms", s.ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", 100.0f * s.ms / scale);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void ProfilerPanel::drawGpuSections(const FrameRecord& rec) {
    // GPU 6 段（§26.2 冻结顺序）：值 <0 = 未就绪/不支持计时查询（延迟回读未完成、
    // 被跳过或从未提交，Profiler 内恒 -1 口径）→ n/a；==0 也按 n/a 保守显示
    //（真实 0ms 的 pass 极少，避免与「不支持」混淆）。
    char buf[64];
    float maxMs = 0.0f;
    for (int i = 0; i < Profiler::kMaxGpuPasses; ++i)
        maxMs = std::max(maxMs, rec.gpuPassMs[i]);
    maxMs = std::max(maxMs, 0.0001f);

    if (ImGui::BeginTable("##gpu_passes", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("GPU 耗时", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("条形", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < Profiler::kMaxGpuPasses; ++i) {
            const char* name = ProfilerSections::gpuPassName(i);
            const float ms = rec.gpuPassMs[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            if (ms < 0.0f) {
                ImGui::TextDisabled("n/a（未就绪）");
            } else if (ms == 0.0f) {
                ImGui::TextDisabled("n/a（不支持）");
            } else {
                ImGui::Text("%s", formatMs(ms, buf, sizeof(buf)));
            }
            ImGui::TableNextColumn();
            if (ms > 0.0f) {
                ImGui::ProgressBar(ms / maxMs, ImVec2(130.0f, 0.0f), "");
            } else {
                ImGui::TextDisabled("—");
            }
        }
        ImGui::EndTable();
    }
}

void ProfilerPanel::drawMemory(const MemorySnapshot& mem) {
    char buf[64];
    if (ImGui::BeginTable("##memory", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("分类", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("占用", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        const auto row = [&](const char* label, const char* value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value);
        };
        row("网格顶点/索引", formatBytes(mem.meshVertexBytes, buf, sizeof(buf)));
        row("贴图（GPU 估算）", formatBytes(mem.textureBytes, buf, sizeof(buf)));
        row("音频（解码 PCM）", formatBytes(mem.audioBytes, buf, sizeof(buf)));
        row("天空盒", formatBytes(mem.cubemapBytes, buf, sizeof(buf)));
        std::snprintf(buf, sizeof(buf), "%zu", mem.materialAssetCount);
        row("材质资产数", buf);
        std::snprintf(buf, sizeof(buf), "%zu", mem.objectCount);
        row("场景对象数", buf);
        std::snprintf(buf, sizeof(buf), "%zu", mem.componentCount);
        row("场景组件数", buf);
        std::snprintf(buf, sizeof(buf), "%zu", mem.loadedSceneCount);
        row("已加载场景数", buf);
        ImGui::EndTable();
    }
}

void ProfilerPanel::onGui(EditorContext& /*ctx*/, EditorCamera& /*camera*/) {
    if (!open) return;

    ImGui::Begin(m_name.c_str(), &open);

    // ===== 控制行 =====
    if (ImGui::Button("Clear")) Profiler::clear();
    ImGui::SameLine();
    ImGui::Checkbox("跟随最新帧", &m_autoFollow);
    ImGui::SameLine();
    ImGui::TextDisabled("(%u/%u 帧)", Profiler::frameCount(), Profiler::capacity());
    ImGui::SameLine();
    ImGui::TextDisabled("| 自身开销 %.3f ms", Profiler::lastOverheadMs());
    ImGui::Separator();

    // ===== 帧时间线（点击定位） =====
    if (ImGui::CollapsingHeader("帧时间线", ImGuiTreeNodeFlags_DefaultOpen)) {
        const FrameRecord* selected = drawTimeline();
        if (selected) {
            ImGui::Separator();
            ImGui::TextUnformatted("CPU 段（堆叠条 = 帧内占比）");
            drawCpuSections(*selected);
            ImGui::Separator();
            ImGui::TextUnformatted("GPU Pass");
            drawGpuSections(*selected);
            ImGui::Separator();
            ImGui::TextUnformatted("内存分类（该帧快照）");
            drawMemory(selected->memory);
        }
    }

    ImGui::End();
}

}  // namespace NovaEditor
