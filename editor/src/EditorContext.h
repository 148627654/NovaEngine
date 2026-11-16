#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>
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
    // 全部选中对象（过滤失效 ID，保持选中顺序；多选批量编辑 / 多选 gizmo 用）
    std::vector<NovaEngine::GameObject*> selectedObjects();
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

    // ---- 运行期设置（不序列化；V0.2-12 / V0.2-13 / V0.3-10 子集）----

    // 吸附参数（V0.2-12）：入口 = Inspector 无选中时 Snap Settings 折叠区；
    // 有效吸附 = enabled 或 Ctrl 按住（GizmoController 内判定）
    struct SnapSettings {
        bool enabled = false;
        glm::vec3 position{0.5f};   // 平移吸附步长（米，逐轴）
        float rotationDeg = 15.0f;  // 旋转吸附步长（度）
        float scale = 0.5f;         // 缩放吸附步长
    };
    SnapSettings snapSettings;

    // 游戏视图分辨率（V0.2-13；仅 Play 生效）：free=跟随面板尺寸；固定分辨率 × renderScale 出图
    struct GameViewSettings {
        int width = 1920;
        int height = 1080;
        bool free = true;
        float renderScale = 1.0f;   // FBO 实际尺寸 = 目标分辨率 × renderScale（0.5–2.0）
    };
    GameViewSettings gameViewSettings;

    // HiDPI（2026-11-16）：显示器内容缩放（glfwGetWindowContentScale，150% = 1.5）。
    // SceneView 用它做 UI 逻辑尺寸→物理像素的倍率（FBO 出图倍率另用 ImGui FramebufferScale）
    float displayContentScale = 1.0f;

    bool followSelected = false;    // Play 相机跟随选中（V0.3-10 子集；SceneView 每帧应用，退出 Play 快照还原）
    bool stepRequested = false;     // Play+Paused 时推进一帧（F10 / Edit→Step）
    double playToastUntil = 0.0;    // 进入 Play 未保存 toast 截止时间戳（glfwGetTime 秒）

private:
    friend class EditorApplication;
    // 进入 Play 前的脏状态：退出 Play 时还原（修复 7.2-4：进 Play 前不脏则 Stop 后不脏）
    bool m_playEntryDirty = false;
};

}  // namespace NovaEditor
