// ReferenceCheckerPanel.cpp —— 引用完整性检查面板（V1.0-7，§26.4）
//
// 扫描按钮（全项目）+ 缺失列表（kind/缺失路径/引用方，双击打开引用方文件定位）
// + Move Asset 对话框（移动/重命名 + 全项目重链）+ 修复后复扫。
// 安全纪律：moveAsset 只在确认模态内由用户显式点击触发（先扫后修、用户确认、不静默改文件）。
// 双击定位：ShellExecute 系统默认程序打开引用方文件（编辑器内打开定位接线归合波日）。
// 存在性判定与扫描器同口径：AssetStream::exists（pak 命中优先、磁盘回退）。

#include "ReferenceCheckerPanel.h"

#include <imgui.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetReferenceScanner.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

// Windows 原生「打开文件」（ShellExecuteW）；NOMINMAX/WIN32_LEAN_AND_MEAN 已由
// nova_configure_target 编译期定义（Common.cmake）
#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <format>

namespace NovaEditor {

ReferenceCheckerPanel::ReferenceCheckerPanel() : Panel("Reference Checker") {}

void ReferenceCheckerPanel::runScan() {
    using namespace NovaEngine;
    m_missing.clear();
    m_totalRefs = 0;

    const std::vector<AssetReference> refs = AssetReferenceScanner::scanProject();
    m_totalRefs = refs.size();
    const std::filesystem::path root = AssetDatabase::projectRoot();
    for (const AssetReference& r : refs) {
        // 相对路径锚项目根（项目外绝对路径经 operator/ 保持绝对，磁盘回退可命中）
        if (AssetStream::exists(root / std::filesystem::path(r.assetPath)))
            continue;
        m_missing.push_back({r.kind, r.assetPath, r.fromFile});
    }
    m_scanned = true;
    Logger::info("引用扫描完成：{} 条引用，{} 条缺失", m_totalRefs, m_missing.size());
}

void ReferenceCheckerPanel::runMoveAsset(const std::string& from, const std::string& to) {
    using namespace NovaEngine;
    // 移动前先复扫（安全纪律：先扫后修——拿到最新引用图再重写）
    if (!m_scanned)
        runScan();

    const int rewritten =
        AssetReferenceScanner::moveAsset(std::filesystem::path(from), std::filesystem::path(to));
    if (rewritten >= 0) {
        m_lastMoveResult = std::format("移动成功：重写 {} 条引用", rewritten);
        Logger::info("Move Asset 完成：{} → {}（重写 {} 条引用）", from, to, rewritten);
        runScan();   // 修复后复扫（清单即时刷新）
    } else {
        m_lastMoveResult = std::format("移动失败：{}（原因详见日志）", from);
    }
}

void ReferenceCheckerPanel::onGui(EditorContext& /*ctx*/ /* 合波日接线后用于编辑器内打开定位 */, EditorCamera&) {
    ImGui::Begin(m_name.c_str(), &open);

    if (ImGui::Button("Scan Project"))
        runScan();
    ImGui::SameLine();
    if (ImGui::Button("Move Asset..."))
        m_showMoveDialog = true;
    ImGui::SameLine();
    ImGui::TextDisabled("(V1.0-7)");

    ImGui::Separator();

    if (!m_scanned) {
        ImGui::TextDisabled("尚未扫描——点击 Scan Project 检查全项目引用");
    } else if (m_missing.empty()) {
        ImGui::Text("全项目引用完整：%zu 条引用零缺失", m_totalRefs);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "缺失引用 %zu 条（共 %zu 条引用）",
                           m_missing.size(), m_totalRefs);
        ImGui::Separator();
        if (ImGui::BeginTable("missing_refs", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Missing Path");
            ImGui::TableSetupColumn("Referenced By");
            ImGui::TableHeadersRow();
            for (const MissingEntry& e : m_missing) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(e.kind.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(e.path.c_str());
                ImGui::TableSetColumnIndex(2);
                // 双击引用方 → 系统默认程序打开定位（合波日可改为编辑器内打开）
                ImGui::Selectable(e.fromFile.c_str(), false);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    const std::filesystem::path abs =
                        NovaEngine::AssetDatabase::projectRoot() /
                        std::filesystem::path(e.fromFile);
                    HINSTANCE result =
                        ShellExecuteW(nullptr, L"open", abs.wstring().c_str(), nullptr, nullptr,
                                      SW_SHOWNORMAL);
                    if (reinterpret_cast<intptr_t>(result) <= 32)
                        NovaEngine::Logger::warn("打开引用方文件失败: {}", abs.string());
                    else
                        NovaEngine::Logger::info("已打开引用方文件: {}", abs.string());
                }
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("双击引用方文件可在外部编辑器中打开定位");
    }

    // ---- Move Asset 对话框（确认模态；用户显式点击「移动并重写」才执行）----
    if (m_showMoveDialog) {
        ImGui::OpenPopup("Move Asset");
        m_showMoveDialog = false;
    }
    if (ImGui::BeginPopupModal("Move Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("移动/重命名资产并重写全项目引用（不可撤销；仅重写已知引用字段）");
        ImGui::InputText("From（项目内相对路径）", m_moveFrom, sizeof(m_moveFrom));
        ImGui::InputText("To（项目内相对路径）", m_moveTo, sizeof(m_moveTo));
        ImGui::Separator();

        const bool valid = m_moveFrom[0] != '\0' && m_moveTo[0] != '\0';
        if (!valid)
            ImGui::BeginDisabled();
        if (ImGui::Button("移动并重写引用")) {
            runMoveAsset(m_moveFrom, m_moveTo);
            ImGui::CloseCurrentPopup();
        }
        if (!valid)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("取消"))
            ImGui::CloseCurrentPopup();

        if (!m_lastMoveResult.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted(m_lastMoveResult.c_str());
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

}  // namespace NovaEditor
