#include "ConsolePanel.h"

#include "../EditorContext.h"

#include <imgui.h>

namespace NovaEditor {

namespace {

// 分级分色（§6.5）：Debug 灰 / Info 白 / Warn 黄 / Error 红
ImVec4 levelColor(NovaEngine::LogLevel level) {
    switch (level) {
    case NovaEngine::LogLevel::Debug: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    case NovaEngine::LogLevel::Info:  return ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
    case NovaEngine::LogLevel::Warn:  return ImVec4(1.00f, 0.85f, 0.30f, 1.0f);
    case NovaEngine::LogLevel::Error: return ImVec4(1.00f, 0.30f, 0.30f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

}  // namespace

ConsolePanel::ConsolePanel() : Panel("Console") {
    NovaEngine::Logger::addSink([this](NovaEngine::LogLevel level, const std::string& msg) {
        m_entries.push_back({level, msg});
        while (m_entries.size() > 500) m_entries.pop_front();  // 环形缓冲上限 500 条
        ++m_newEntries;
    });
}

ConsolePanel::~ConsolePanel() {
    NovaEngine::Logger::clearSinks();  // 注销 sink，防止悬垂 this
}

void ConsolePanel::onGui(EditorContext&, EditorCamera&) {
    ImGui::Begin(m_name.c_str(), &open);

    ImGui::Checkbox("Debug", &m_showDebug); ImGui::SameLine();
    ImGui::Checkbox("Info", &m_showInfo); ImGui::SameLine();
    ImGui::Checkbox("Warn", &m_showWarn); ImGui::SameLine();
    ImGui::Checkbox("Error", &m_showError); ImGui::SameLine();
    ImGui::Checkbox("Auto Scroll", &m_autoScroll); ImGui::SameLine();
    if (ImGui::Button("Clear")) m_entries.clear();
    ImGui::Separator();

    ImGui::BeginChild("##console_log", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& e : m_entries) {
        if (e.level == NovaEngine::LogLevel::Debug && !m_showDebug) continue;
        if (e.level == NovaEngine::LogLevel::Info && !m_showInfo) continue;
        if (e.level == NovaEngine::LogLevel::Warn && !m_showWarn) continue;
        if (e.level == NovaEngine::LogLevel::Error && !m_showError) continue;
        ImGui::TextColored(levelColor(e.level), "%s", e.message.c_str());
    }
    if (m_autoScroll && m_newEntries > 0) ImGui::SetScrollHereY(1.0f);
    m_newEntries = 0;
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace NovaEditor
