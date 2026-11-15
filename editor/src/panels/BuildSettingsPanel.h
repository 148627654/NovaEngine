#pragma once
#include "../Panel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace NovaEditor {

// Build Settings 面板（V0.3-12，波 3，第 8 面板）：
// 扫描 assets/scenes/*.nova.json → 勾选 + 上下排序 → Save 写 assets/build.novabuild
//（SceneManager::saveBuildSettings）。列表语义 = SceneManager::loadScene 的候选集。
class BuildSettingsPanel : public Panel {
public:
    explicit BuildSettingsPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    struct Entry {
        std::string name;                 // 场景名 = 文件名去 ".nova.json"（SceneManager 键）
        std::filesystem::path path;       // 相对项目根
        bool enabled = false;
    };

    // P2-1 修复：rescan 不再每帧执行——仅首次打开与显式 Rescan 按钮触发；
    // 且保留既有顺序与勾选状态、仅追加新文件（手动 Up/Down 排序持久）
    void rescan();
    std::vector<Entry> m_entries;
    bool m_needsRescan = true;            // 构造后首次 onGui 触发扫描
};

}  // namespace NovaEditor
