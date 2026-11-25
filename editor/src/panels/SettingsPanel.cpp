#include "SettingsPanel.h"

#include "../EditorSettings.h"

#include <imgui.h>

#include <NovaEngine/Core/Logger.h>

#include <cstring>

namespace NovaEditor {

SettingsPanel::SettingsPanel() : Panel("Settings") {
    open = false;  // 默认关：View 菜单 / Edit → Editor Settings… 打开
}

namespace {

// 自定义样式编辑的精选颜色集（ImGuiCol 枚举 + 显示名）。
// 不用 imgui_demo 的 ShowStyleEditor（imgui_demo.cpp 未编进编辑器 target）
const std::pair<int, const char*> kStyleColorEdit[] = {
    {ImGuiCol_Text, "Text"},               {ImGuiCol_TextDisabled, "Text Disabled"},
    {ImGuiCol_WindowBg, "Window Bg"},      {ImGuiCol_ChildBg, "Child Bg"},
    {ImGuiCol_PopupBg, "Popup Bg"},        {ImGuiCol_Border, "Border"},
    {ImGuiCol_FrameBg, "Frame Bg"},        {ImGuiCol_FrameBgHovered, "Frame Bg Hover"},
    {ImGuiCol_FrameBgActive, "Frame Bg Active"}, {ImGuiCol_TitleBg, "Title Bg"},
    {ImGuiCol_TitleBgActive, "Title Bg Active"}, {ImGuiCol_MenuBarBg, "Menu Bar Bg"},
    {ImGuiCol_Header, "Header"},           {ImGuiCol_HeaderHovered, "Header Hover"},
    {ImGuiCol_HeaderActive, "Header Active"}, {ImGuiCol_Button, "Button"},
    {ImGuiCol_ButtonHovered, "Button Hover"}, {ImGuiCol_ButtonActive, "Button Active"},
    {ImGuiCol_CheckMark, "Check Mark"},    {ImGuiCol_SliderGrab, "Slider Grab"},
    {ImGuiCol_SliderGrabActive, "Slider Grab Active"}, {ImGuiCol_Tab, "Tab"},
    {ImGuiCol_TabHovered, "Tab Hover"},    {ImGuiCol_TabActive, "Tab Active"},
    {ImGuiCol_Separator, "Separator"},     {ImGuiCol_DockingPreview, "Docking Preview"},
    {ImGuiCol_DockingEmptyBg, "Docking Empty Bg"},
};

}  // namespace

void SettingsPanel::onGui(EditorContext& ctx, EditorCamera& camera) {
    (void)ctx;
    (void)camera;  // 设置面板不使用场景上下文与编辑器相机
    EditorSettings& settings = EditorSettings::instance();
    auto& data = settings.data;

    ImGui::Begin(m_name.c_str(), &open);
    ImGui::TextDisabled("存储: %s", EditorSettings::filePath().string().c_str());

    // ===== 主题（v1：dark / light / classic 三套预置 + ImGuiStyle 自定义）=====
    if (ImGui::CollapsingHeader("Theme", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* themes[] = {"dark", "light", "classic"};
        int cur = 0;
        for (int i = 0; i < 3; ++i)
            if (data.theme == themes[i]) cur = i;
        if (ImGui::Combo("Preset", &cur, themes, 3)) {
            data.theme = themes[cur];
            EditorSettings::applyTheme(data.theme);
            if (data.styleCustom) EditorSettings::applyStyleCustom(data);  // 预置 + 自定义叠加
            settings.markDirty();
        }
        // 自定义：编辑「当前实时样式」→ 保存按钮捕获落盘（v1 简化编辑：精选色 + 圆角，
        // 不用 imgui_demo 的 ShowStyleEditor——imgui_demo.cpp 未编进编辑器 target）
        ImGui::TextDisabled("Custom style (edit live, then save):");
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushItemWidth(150.0f);
        for (const auto& [col, label] : kStyleColorEdit) {
            ImGui::ColorEdit4(label, &style.Colors[col].x, ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine(240.0f);
            ImGui::TextDisabled("%d", col);
        }
        ImGui::SliderFloat("Window Rounding", &style.WindowRounding, 0.0f, 16.0f, "%.1f");
        ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 16.0f, "%.1f");
        ImGui::PopItemWidth();
        if (ImGui::Button("Save Current Style As Custom")) {
            EditorSettings::captureStyle(data);
            settings.markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Custom Style") && data.styleCustom) {
            data.styleCustom = false;
            data.styleColors.clear();
            data.windowRounding = -1.0f;
            data.frameRounding = -1.0f;
            EditorSettings::applyTheme(data.theme);  // 回到纯预置
            settings.markDirty();
        }
        if (data.styleCustom)
            ImGui::TextDisabled("Custom style active (%zu colors)", data.styleColors.size());
    }

    // ===== 快捷键（v1：静态映射表；核心动作不可移除，重绑做冲突检测）=====
    if (ImGui::CollapsingHeader("Keybindings")) {
        const auto& table = EditorSettings::defaultKeybindings();
        if (m_chordBufs.empty()) {
            m_chordBufs.reserve(table.size());
            for (const auto& [action, defChord] : table) {
                std::pair<std::string, char[64]> entry;
                entry.first = action;
                std::snprintf(entry.second, sizeof(entry.second), "%s", defChord.c_str());
                m_chordBufs.emplace_back(std::move(entry));
            }
        }
        // P2-3：每帧从当前生效绑定同步缓冲（未在编辑中时）——重绑/Reset 后显示生效值
        if (!m_chordEditing) {
            for (auto& [action, buf] : m_chordBufs)
                std::snprintf(buf, sizeof(buf), "%s", EditorSettings::chordFor(action).c_str());
        }
        for (auto& [action, buf] : m_chordBufs) {
            ImGui::PushID(action.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(action.c_str());
            ImGui::SameLine(160.0f);
            ImGui::SetNextItemWidth(180.0f);
            const bool edited = ImGui::InputText("##chord", buf, sizeof(buf));
            if (ImGui::IsItemActive()) m_chordEditing = true;   // 编辑中：暂停同步防覆盖输入
            if (ImGui::IsItemDeactivatedAfterEdit() && edited) {
                m_chordEditing = false;
                const std::string conflict =
                    EditorSettings::checkConflict(data, action, std::string(buf));
                bool c = false, s = false, a = false;
                int k = -1;
                const bool valid = EditorSettings::parseChord(std::string(buf), c, s, a, k);
                if (!valid || !conflict.empty()) {
                    // 非法或冲突：回退并提示（核心动作不可移除，冲突禁止静默覆盖）
                    std::snprintf(buf, sizeof(buf), "%s", EditorSettings::chordFor(action).c_str());
                    NovaEngine::Logger::warn("快捷键重绑被拒绝: {} → {}（{}）", action, buf,
                                             !valid ? "chord 非法" : ("与 " + conflict + " 冲突").c_str());
                } else {
                    data.keybindings[action] = buf;
                    settings.markDirty();
                }
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("Reset Keybindings")) {
            for (const auto& [action, defChord] : table) data.keybindings[action] = defChord;
            for (auto& [action, buf] : m_chordBufs)
                std::snprintf(buf, sizeof(buf), "%s", EditorSettings::chordFor(action).c_str());
            settings.markDirty();
        }
        ImGui::TextDisabled("Chord 格式: Ctrl+Shift+R / F10（核心动作固定，可重绑，冲突自动拒绝）");
    }

    // ===== 多视口（ImGui ViewportsEnable；重启生效）=====
    if (ImGui::CollapsingHeader("Multi-Viewport")) {
        if (ImGui::Checkbox("Enable Multi-Viewport", &data.multiViewport)) settings.markDirty();
        ImGui::TextDisabled("重启编辑器生效（ImGui 上下文配置期读取；评估结论见 KNOWN-ISSUES）");
    }

    ImGui::End();
}

}  // namespace NovaEditor
