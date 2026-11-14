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
            // V0.3-5：隐藏点开头条目（.novacache 等）与贴图 .novameta 伴随文件
            const std::string fname = de.path().filename().string();
            if (!fname.empty() && fname.front() == '.') continue;
            std::string fl = fname;
            std::transform(fl.begin(), fl.end(), fl.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (fl.ends_with(".novameta")) continue;
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

            // 贴图右键菜单：Texture Import Settings…（V0.3-5 .novameta 编辑入口）
            if (!e.isDir && DragDrop::isTextureFile(e.rel)) {
                const std::string ctxId = "asset_ctx_" + e.rel.string();
                if (ImGui::BeginPopupContextItem(ctxId.c_str())) {
                    if (ImGui::MenuItem("Texture Import Settings...")) {
                        // 打开模态前 loadOrCreate 读取当前值（meta 缺失时自动生成默认并落盘）
                        const fs::path abs = root / e.rel;
                        bool metaExisted = false;
                        m_metaEditSettings = NovaEngine::TextureMeta::loadOrCreate(abs, metaExisted);
                        m_metaEditTexPath = abs;
                        m_metaEditOpen = true;
                        if (!metaExisted)
                            NovaEngine::Logger::info("已为贴图生成默认导入设置: {}",
                                                     e.rel.string() + ".novameta");
                    }
                    ImGui::EndPopup();
                }
            }
        };
        for (const auto& e : dirs) drawEntry(e);
        for (const auto& e : files) drawEntry(e);
    }
    ImGui::EndChild();

    // ---- 贴图导入设置模态（V0.3-5：colorSpace/wrap/filter/mipmaps + Save/Reset/Close）----
    if (m_metaEditOpen) {
        using CS = NovaEngine::TextureImportSettings::ColorSpace;
        ImGui::OpenPopup("Texture Import Settings");
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
        bool stayOpen = true;
        if (ImGui::BeginPopupModal("Texture Import Settings", &stayOpen,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
            // 标题 = 项目内相对路径（跨盘等异常时回退绝对路径）
            std::error_code rec;
            const fs::path rel =
                fs::relative(m_metaEditTexPath, NovaEngine::AssetDatabase::projectRoot(), rec);
            ImGui::TextUnformatted(
                rec ? m_metaEditTexPath.string().c_str() : rel.generic_string().c_str());
            ImGui::Separator();

            const char* csName =
                m_metaEditSettings.colorSpace == CS::Srgb ? "sRGB" : "Linear";
            if (ImGui::BeginCombo("Color Space", csName)) {
                if (ImGui::Selectable("sRGB", m_metaEditSettings.colorSpace == CS::Srgb))
                    m_metaEditSettings.colorSpace = CS::Srgb;
                if (ImGui::Selectable("Linear", m_metaEditSettings.colorSpace == CS::Linear))
                    m_metaEditSettings.colorSpace = CS::Linear;
                ImGui::EndCombo();
            }

            const char* wrapName = m_metaEditSettings.wrapMode == NovaEngine::TextureWrap::Repeat
                                       ? "Repeat"
                                       : "Clamp To Edge";
            if (ImGui::BeginCombo("Wrap Mode", wrapName)) {
                if (ImGui::Selectable("Repeat",
                                      m_metaEditSettings.wrapMode == NovaEngine::TextureWrap::Repeat))
                    m_metaEditSettings.wrapMode = NovaEngine::TextureWrap::Repeat;
                if (ImGui::Selectable("Clamp To Edge",
                                      m_metaEditSettings.wrapMode ==
                                          NovaEngine::TextureWrap::ClampToEdge))
                    m_metaEditSettings.wrapMode = NovaEngine::TextureWrap::ClampToEdge;
                ImGui::EndCombo();
            }

            const char* filterName =
                m_metaEditSettings.filterMode == NovaEngine::TextureFilter::Bilinear
                    ? "Bilinear"
                    : "Trilinear";
            if (ImGui::BeginCombo("Filter Mode", filterName)) {
                if (ImGui::Selectable(
                        "Bilinear",
                        m_metaEditSettings.filterMode == NovaEngine::TextureFilter::Bilinear))
                    m_metaEditSettings.filterMode = NovaEngine::TextureFilter::Bilinear;
                if (ImGui::Selectable(
                        "Trilinear",
                        m_metaEditSettings.filterMode == NovaEngine::TextureFilter::Trilinear))
                    m_metaEditSettings.filterMode = NovaEngine::TextureFilter::Trilinear;
                ImGui::EndCombo();
            }

            ImGui::Checkbox("Generate Mipmaps", &m_metaEditSettings.generateMipmaps);

            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (NovaEngine::TextureMeta::save(m_metaEditTexPath, m_metaEditSettings)) {
                    NovaEngine::AssetDatabase::instance().clearCache();  // meta 变化 → 缓存失效重导
                    NovaEngine::Logger::info("贴图导入设置已保存: {}.novameta",
                                             m_metaEditTexPath.string());
                    m_metaEditOpen = false;
                } else {
                    NovaEngine::Logger::error("贴图导入设置保存失败: {}",
                                              m_metaEditTexPath.string());
                }
            }
            ImGui::SameLine();
            // 字段恢复默认（不落盘；确认生效需再点 Save）
            if (ImGui::Button("Reset to Default", ImVec2(140, 0)))
                m_metaEditSettings = NovaEngine::TextureImportSettings{};
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(80, 0))) m_metaEditOpen = false;
            ImGui::EndPopup();
        }
        if (!stayOpen && m_metaEditOpen) m_metaEditOpen = false;
    }

    ImGui::End();
}

}  // namespace NovaEditor
