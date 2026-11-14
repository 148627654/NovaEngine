#pragma once
#include <filesystem>
#include <memory>
#include <vector>

#include "EditorCamera.h"
#include "EditorContext.h"
#include "GizmoController.h"
#include "Panel.h"

namespace NovaEngine {
class Window;
class Scene;
}  // namespace NovaEngine

namespace NovaEditor {

class StatsPanel;

// 应用外壳（§2.9 + §3.1）：主循环编排 / docking 布局 / Play 状态切换 / 外部拖放分发 / 文件菜单 /
// Undo-Redo 入口（V0.2-1）/ 未保存确认模态（V0.2-2）/ Stats 帧时序集成（V0.2-11）
class EditorApplication {
public:
    explicit EditorApplication(std::unique_ptr<NovaEngine::Window> window);
    ~EditorApplication();

    int run();   // init → 主循环 → shutdown

private:
    // 未保存确认挂起动作（V0.2-2 §4.2）
    enum class PendingAction { None, NewScene, OpenScene, ExitApp };

    void init();     // 面板 + 默认场景加载
    void shutdown(); // ImGui / Renderer 清理（窗口由析构最后释放）
    void update(float dt);   // §3.1 伪代码
    void render();
    void handleExternalDrop(const std::vector<std::filesystem::path>& paths);
    void enterPlayMode();
    void exitPlayMode();
    void newScene();                          // 入口：Play 拦截 + 判脏弹模态
    void newSceneInternal();                  // 原逻辑（末尾压 Undo 基线）
    void openScene(const std::filesystem::path& p);  // 入口：同上
    void loadSceneInternal(const std::filesystem::path& p);
    void openSceneDialog();
    bool saveScene();                         // 返回 false = 保存失败/取消
    bool saveSceneAs();
    void showSaveConfirmModal();              // Save / Don't Save / Cancel 三按钮（Save 失败不关模态）
    void processPendingAction();              // 模态决议后执行挂起动作
    void requestExit();                       // 窗口关闭拦截与 File→Exit 统一出口
    void undoScene();
    void redoScene();
    void updateWindowTitle();                 // 每帧：场景名 + 脏标记 * + " - NovaEditor"
    void buildDockLayout(ImGuiID dockspaceId);
    void applyEditorCameraFromScene();
    void showMainMenu();
    void handleGlobalShortcuts();

    std::unique_ptr<NovaEngine::Window> m_window;
    std::unique_ptr<NovaEngine::Scene> m_scene;
    EditorContext m_context;
    EditorCamera m_camera;
    std::vector<std::unique_ptr<Panel>> m_panels;
    StatsPanel* m_statsPanel = nullptr;       // F8 开关；View 菜单经 m_panels 循环自动包含
    GizmoController m_gizmo;
    std::vector<std::filesystem::path> m_pendingDrops;  // GLFW 拖放回调写入，update 末尾消费
    bool m_firstFrame = true;

    // 未保存确认状态机（V0.2-2）
    PendingAction m_pendingAction = PendingAction::None;
    std::filesystem::path m_pendingOpenPath;  // OpenScene 挂起时暂存路径
    bool m_showSaveConfirm = false;
    bool m_exitRequested = false;
};

}  // namespace NovaEditor
