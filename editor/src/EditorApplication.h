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

// 应用外壳（§2.9 + §3.1）：主循环编排 / docking 布局 / Play 状态切换 / 外部拖放分发 / 文件菜单
class EditorApplication {
public:
    explicit EditorApplication(std::unique_ptr<NovaEngine::Window> window);
    ~EditorApplication();

    int run();   // init → 主循环 → shutdown

private:
    void init();     // 面板 + 默认场景加载
    void shutdown(); // ImGui / Renderer 清理（窗口由析构最后释放）
    void update(float dt);   // §3.1 伪代码
    void render();
    void handleExternalDrop(const std::vector<std::filesystem::path>& paths);
    void enterPlayMode();
    void exitPlayMode();
    void newScene();
    void openScene(const std::filesystem::path& p);
    void openSceneDialog();
    void saveScene();
    void saveSceneAs();
    void buildDockLayout(ImGuiID dockspaceId);
    void applyEditorCameraFromScene();
    void showMainMenu();
    void handleGlobalShortcuts();

    std::unique_ptr<NovaEngine::Window> m_window;
    std::unique_ptr<NovaEngine::Scene> m_scene;
    EditorContext m_context;
    EditorCamera m_camera;
    std::vector<std::unique_ptr<Panel>> m_panels;
    GizmoController m_gizmo;
    std::vector<std::filesystem::path> m_pendingDrops;  // GLFW 拖放回调写入，update 末尾消费
    bool m_firstFrame = true;
};

}  // namespace NovaEditor
