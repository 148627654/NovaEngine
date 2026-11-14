#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

#include <imgui.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>

#include "UndoRedoManager.h"

namespace NovaEditor {

class EditorApplication;

// 编辑器全局状态（§2.9）：选中集 / Edit-Play 状态 / 场景指针 / 快照 / 脏标记 / 面板区域上报 /
// Undo-Redo 栈（V0.2-1）
class EditorContext {
public:
    enum class EditorState { Edit, Play };

    EditorState state = EditorState::Edit;
    bool paused = false;                     // Play 内暂停（scene->update 停摆）
    NovaEngine::Scene* scene = nullptr;

    std::unordered_set<uint64_t> selectedIds;

    // 恰好单选时返回该对象，否则返回 nullptr（并顺手清理失效 ID）
    NovaEngine::GameObject* activeObject();
    void setSelected(uint64_t id);
    void toggleSelected(uint64_t id);        // Ctrl 加选/减选
    void clearSelection();
    bool isSelected(uint64_t id) const;

    std::filesystem::path currentScenePath;  // 空 = 未保存
    bool sceneDirty = false;
    std::string playSnapshot;                // 进入 Play 时的 JSON 快照

    UndoRedoManager undoRedo;                // 快照式撤销栈（V0.2-1）

    // 脏标记漏斗（V0.2-2 裁定 4）：
    void markDirtyOnly();                    // 只置脏（拖拽/输入中间帧用，不推快照防刷栈）
    void markSceneDirty();                   // 置脏 + Edit 态推快照（Play 态只置脏不入栈）

    // 面板每帧上报自身屏幕区域（外部拖放目标判定用；EditorApplication 每帧先清零）
    ImVec2 viewportScreenMin{}, viewportScreenMax{};          // SceneView
    ImVec2 assetBrowserScreenMin{}, assetBrowserScreenMax{};  // AssetBrowser

private:
    friend class EditorApplication;
    // 进入 Play 前的脏状态：退出 Play 时还原（修复 7.2-4：进 Play 前不脏则 Stop 后不脏）
    bool m_playEntryDirty = false;
};

}  // namespace NovaEditor
