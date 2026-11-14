#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

#include <imgui.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>

namespace NovaEditor {

// 编辑器全局状态（§2.9）：选中集 / Edit-Play 状态 / 场景指针 / 快照 / 脏标记 / 面板区域上报
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

    // 面板每帧上报自身屏幕区域（外部拖放目标判定用；EditorApplication 每帧先清零）
    ImVec2 viewportScreenMin{}, viewportScreenMax{};          // SceneView
    ImVec2 assetBrowserScreenMin{}, assetBrowserScreenMax{};  // AssetBrowser
};

}  // namespace NovaEditor
