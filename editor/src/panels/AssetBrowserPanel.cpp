#include "AssetBrowserPanel.h"

#include "../DragDrop.h"
#include "../EditorContext.h"

#include <imgui.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace NovaEditor {

namespace {

struct Entry {
    std::filesystem::path rel;  // 相对项目根
    bool isDir = false;
};

// 不区分大小写的文件名排序（目录在前由调用方保证）
bool byName(const Entry& a, const Entry& b) {
    std::string an = a.rel.filename().string(), bn = b.rel.filename().string();
    std::transform(an.begin(), an.end(), an.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(bn.begin(), bn.end(), bn.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return an < bn;
}

}  // namespace

AssetBrowserPanel::AssetBrowserPanel() : Panel("Assets") {}

void AssetBrowserPanel::onGui(EditorContext& ctx, EditorCamera&) {
    namespace fs = std::filesystem;
    ImGui::Begin(m_name.c_str(), &open);

    // 上报屏幕区域（外部拖放目标判定用）
    ctx.assetBrowserScreenMin = ImGui::GetWindowPos();
    ctx.assetBrowserScreenMax = ImGui::GetWindowPos() + ImGui::GetWindowSize();

    const fs::path root = NovaEngine::AssetDatabase::projectRoot();
    const fs::path full = root / m_currentDir;

    // 面包屑导航（assets > models > ...）
    if (ImGui::Button("assets")) m_currentDir = "assets";
    fs::path cursor;
    for (auto it = m_currentDir.begin(); it != m_currentDir.end(); ++it) {
        if (*it == "assets") {
            cursor = *it;
            continue;
        }
        cursor /= *it;
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        if (ImGui::Button(it->string().c_str())) m_currentDir = cursor;
    }
    ImGui::Separator();

    ImGui::BeginChild("##asset_entries", ImVec2(0.0f, 0.0f), false);
    std::error_code ec;
    if (!fs::exists(full, ec)) {
        ImGui::TextUnformatted("Directory does not exist: assets/");
    } else {
        std::vector<Entry> dirs, files;
        for (fs::directory_iterator it(full, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::directory_entry& de = *it;
            fs::path rel = fs::relative(de.path(), root, ec);
            if (ec) { ec.clear(); continue; }
            if (de.is_directory(ec)) dirs.push_back({rel, true});
            else if (de.is_regular_file(ec)) files.push_back({rel, false});
            ec.clear();
        }
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        // 上级目录
        if (m_currentDir != "assets")
            if (ImGui::Selectable("[ .. ]", false))
                m_currentDir = m_currentDir.parent_path();

        auto drawEntry = [&](const Entry& e) {
            const std::string prefix =
                e.isDir ? "[D] "
                : DragDrop::isModelFile(e.rel) ? "[M] "
                : DragDrop::isTextureFile(e.rel) ? "[T] "
                : DragDrop::isSceneFile(e.rel) ? "[S] "
                                               : "[F] ";
            const std::string label = prefix + e.rel.filename().string();
            ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", e.rel.string().c_str());
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (e.isDir) {
                        m_currentDir = e.rel;  // 双击目录进入
                    } else if (DragDrop::isSceneFile(e.rel)) {
                        // 双击 .nova.json 加载场景
                        if (ctx.state == EditorContext::EditorState::Edit) {
                            if (openSceneCallback) openSceneCallback(e.rel);
                        } else {
                            NovaEngine::Logger::warn("Play 模式下不可加载场景");
                        }
                    }
                }
            }

            // 模型/贴图文件为拖拽源（payload = 绝对路径 UTF-8 字符串，含结尾 \0）
            if (!e.isDir && (DragDrop::isModelFile(e.rel) || DragDrop::isTextureFile(e.rel))) {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    const std::string abs = (root / e.rel).string();
                    ImGui::SetDragDropPayload(DragDrop::kAssetPayload, abs.c_str(), abs.size() + 1);
                    ImGui::TextUnformatted(e.rel.filename().string().c_str());
                    ImGui::EndDragDropSource();
                }
            }
        };
        for (const auto& e : dirs) drawEntry(e);
        for (const auto& e : files) drawEntry(e);
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace NovaEditor
