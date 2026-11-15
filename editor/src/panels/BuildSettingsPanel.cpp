#include "BuildSettingsPanel.h"

#include "../EditorContext.h"

#include <imgui.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/SceneManager.h>

#include <algorithm>
#include <string>

namespace NovaEditor {

BuildSettingsPanel::BuildSettingsPanel() : Panel("Build Settings") {}

void BuildSettingsPanel::rescan() {
    // 扫描 assets/scenes/*.nova.json（相对项目根）。
    // P2-1 修复：保留既有顺序与勾选状态（按 path 匹配），仅追加新文件（默认勾选）——
    // 手动 Up/Down 排序在后续帧与再次 Rescan 时持久；首次扫描（无既有状态）按名排序。
    // 目录不存在 → 空列表（不崩溃）。
    std::vector<Entry> scanned;
    const std::filesystem::path dir = NovaEngine::AssetDatabase::projectRoot() / "assets" / "scenes";
    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec) || ec) continue;
            const std::filesystem::path p = entry.path();
            if (p.extension() != ".json" || !p.filename().string().ends_with(".nova.json")) continue;
            Entry e;
            e.path = std::filesystem::relative(p, NovaEngine::AssetDatabase::projectRoot(), ec);
            if (ec) { ec.clear(); continue; }
            e.name = p.stem().stem().string();   // "demo.nova.json" → stem "demo.nova" → "demo"
            if (e.name.empty()) continue;
            e.enabled = true;   // 新文件默认勾选（既有文件的状态在合并时保留）
            scanned.push_back(std::move(e));
        }
    }
    std::sort(scanned.begin(), scanned.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });

    // 合并：既有条目按原顺序保留（含勾选状态），已删除的文件条目丢弃；剩余新文件按名追加
    std::vector<Entry> merged;
    merged.reserve(scanned.size());
    for (const Entry& old : m_entries) {
        auto it = std::find_if(scanned.begin(), scanned.end(),
                               [&](const Entry& s) { return s.path == old.path; });
        if (it == scanned.end())
            continue;   // 文件已被删除 → 丢弃该条目
        it->enabled = old.enabled;   // 保留勾选状态
        merged.push_back(std::move(*it));
        scanned.erase(it);
    }
    for (Entry& s : scanned)
        merged.push_back(std::move(s));
    m_entries = std::move(merged);
}

void BuildSettingsPanel::onGui(EditorContext& /*ctx*/ /* DevOps 修复 C4100：波 3 暂未使用，保留参数名注释 */, EditorCamera&) {
    using namespace NovaEngine;
    ImGui::Begin(m_name.c_str(), &open);

    // P2-1 修复：不再每帧扫描（会覆盖手动排序）——仅首次打开与显式 Rescan 按钮触发
    if (m_needsRescan) {
        rescan();
        m_needsRescan = false;
    }

    ImGui::TextUnformatted("Build scene list (assets/scenes/*.nova.json)");
    ImGui::TextDisabled("Checked scenes are loadable via SceneManager::loadScene at runtime");
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) m_needsRescan = true;   // 手动拾取新增场景文件（保留排序/勾选）
    ImGui::Separator();

    if (m_entries.empty()) {
        ImGui::TextDisabled("No scene files found in assets/scenes/");
    }

    int moveFrom = -1, moveTo = -1;   // 上下排序（延迟到绘制结束统一执行，避免遍历中交换）
    for (size_t i = 0; i < m_entries.size(); ++i) {
        Entry& e = m_entries[i];
        ImGui::PushID(&e);
        ImGui::Checkbox("##enabled", &e.enabled);
        ImGui::SameLine();
        ImGui::TextUnformatted(e.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Up")) {
            if (i > 0) { moveFrom = static_cast<int>(i); moveTo = static_cast<int>(i) - 1; }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Down")) {
            if (i + 1 < m_entries.size()) { moveFrom = static_cast<int>(i); moveTo = static_cast<int>(i) + 1; }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", e.path.string().c_str());
        ImGui::PopID();
    }
    if (moveFrom >= 0)
        std::swap(m_entries[static_cast<size_t>(moveFrom)], m_entries[static_cast<size_t>(moveTo)]);

    ImGui::Separator();
    ImGui::TextDisabled("Saved to assets/build.novabuild (SceneManager reads it at init)");
    if (ImGui::Button("Save Build Settings")) {
        // 勾选项按当前展示顺序持久化；路径校验由 SceneManager 侧跳过空条目
        std::vector<SceneBuildInfo> scenes;
        for (const Entry& e : m_entries)
            if (e.enabled)
                scenes.push_back(SceneBuildInfo{e.name, e.path});
        SceneManager::saveBuildSettings(scenes);
    }

    ImGui::End();
}

}  // namespace NovaEditor
