#include "EditorApplication.h"

#include "DragDrop.h"
#include "panels/AssetBrowserPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/SceneViewPanel.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder* API（ImGui 1.90+ 从 imgui.h 移入 internal）
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <glad/gl.h>

#include <NovaEngine/NovaEngine.h>

#include <cwchar>
#include <string>

// 文件对话框（Windows 原生）。NOMINMAX / WIN32_LEAN_AND_MEAN 已由 nova_configure_target 定义，
// 此处兜底防止未走 CMake 时宏冲突；glad 已先行包含，避免 APIENTRY 重定义。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// 顺序要求：glad 先行避免 APIENTRY 重定义；windows.h 必须在 commdlg.h 之前
//（commdlg.h 会拉入 prsht.h，后者依赖 windows.h 里的 CALLBACK 等类型）
#include <windows.h>
#include <commdlg.h>

namespace NovaEditor {

namespace {

// 保存场景文件对话框（确保 .nova.json 扩展名）
std::filesystem::path showSaveSceneDialog(const std::filesystem::path& initialDir,
                                          const std::string& initialName) {
    wchar_t buf[1024] = {};
    const std::wstring wname = std::filesystem::path(initialName).wstring();
    if (wname.size() < 1024) wcscpy_s(buf, wname.c_str());
    const wchar_t kFilter[] = L"Nova Scene (*.nova.json)\0*.nova.json\0All Files (*.*)\0*.*\0";
    const std::wstring wdir = initialDir.wstring();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.lpstrInitialDir = wdir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};

    std::filesystem::path p{std::wstring(buf)};  // 花括号初始化：避免 Most Vexing Parse
    const std::string fn = p.filename().string();
    if (!fn.ends_with(".nova.json")) {
        if (p.extension() == ".json")
            p = p.parent_path() / (p.stem().string() + ".nova.json");
        else
            p += ".nova.json";
    }
    return p;
}

// 打开场景文件对话框
std::filesystem::path showOpenSceneDialog(const std::filesystem::path& initialDir) {
    wchar_t buf[1024] = {};
    const wchar_t kFilter[] = L"Nova Scene (*.nova.json)\0*.nova.json\0All Files (*.*)\0*.*\0";
    const std::wstring wdir = initialDir.wstring();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.lpstrInitialDir = wdir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return std::filesystem::path(std::wstring(buf));
}

// 鼠标是否落在面板上报区域内（min==max 视为未上报）
bool insideRect(const ImVec2& p, const ImVec2& mn, const ImVec2& mx) {
    if (mx.x <= mn.x || mx.y <= mn.y) return false;
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y;
}

}  // namespace

EditorApplication::EditorApplication(std::unique_ptr<NovaEngine::Window> window)
    : m_window(std::move(window)) {}

EditorApplication::~EditorApplication() = default;

void EditorApplication::init() {
    using namespace NovaEngine;

    // 外部文件拖放（Explorer）→ 队列，update 末尾按鼠标位置分发
    m_window->setDropCallback([this](const std::vector<std::filesystem::path>& paths) {
        m_pendingDrops.insert(m_pendingDrops.end(), paths.begin(), paths.end());
    });

    m_scene = std::make_unique<Scene>("Untitled");
    m_context.scene = m_scene.get();
    applyEditorCameraFromScene();

    // 五面板（§6.5 布局：左 Hierarchy / 右 Inspector / 底 Console+Assets / 中 Scene）
    auto scenePanel = std::make_unique<SceneViewPanel>();
    scenePanel->m_gizmo = &m_gizmo;
    m_panels.push_back(std::move(scenePanel));
    m_panels.push_back(std::make_unique<HierarchyPanel>());
    m_panels.push_back(std::make_unique<InspectorPanel>());
    m_panels.push_back(std::make_unique<ConsolePanel>());
    auto browser = std::make_unique<AssetBrowserPanel>();
    browser->openSceneCallback = [this](const std::filesystem::path& p) { openScene(p); };
    m_panels.push_back(std::move(browser));

    // 默认加载示例场景（不存在则保持空场景）
    const std::filesystem::path demo =
        AssetDatabase::projectRoot() / "assets" / "scenes" / "demo.nova.json";
    std::error_code ec;
    if (std::filesystem::exists(demo, ec)) {
        openScene(demo);
    } else {
        Logger::info("未找到示例场景 {}，使用空场景", demo.string());
    }

    Logger::info("NovaEditor 初始化完成");
}

void EditorApplication::shutdown() {
    using namespace NovaEngine;
    m_panels.clear();  // ConsolePanel 析构时注销 Logger sink
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    Renderer::shutdown();
    Logger::info("NovaEditor 退出");
}

int EditorApplication::run() {
    using namespace NovaEngine;
    init();
    while (!m_window->shouldClose()) {
        m_window->pollEvents();  // GLFW 事件 → Input 原始状态 + 拖放回调队列
        Time::beginFrame();
        Input::beginFrame();
        update(Time::deltaTime());
        render();
        m_window->swapBuffers();
    }
    shutdown();
    return 0;
}

void EditorApplication::update(float dt) {
    using namespace NovaEngine;

    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // Docking 主区域；首帧用 DockBuilder 摆五面板（§6.5）
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (m_firstFrame) {
        buildDockLayout(dockspaceId);
        m_firstFrame = false;
    }

    // Play 状态机（§7）
    if (m_context.state == EditorContext::EditorState::Play) {
        if (!m_context.paused) m_scene->update(dt);
    } else {
        m_scene->flushDestroyQueue();
        // 编辑器相机状态回写场景（随 Ctrl+S 持久化）
        m_scene->editorCameraTarget = m_camera.target;
        m_scene->editorCameraDistance = m_camera.distance;
        m_scene->editorCameraYaw = m_camera.yaw;
        m_scene->editorCameraPitch = m_camera.pitch;
    }

    showMainMenu();
    handleGlobalShortcuts();

    // 面板区域上报每帧先清零（未渲染的面板不残留旧区域）
    m_context.viewportScreenMin = m_context.viewportScreenMax = ImVec2(0.0f, 0.0f);
    m_context.assetBrowserScreenMin = m_context.assetBrowserScreenMax = ImVec2(0.0f, 0.0f);
    for (auto& panel : m_panels)
        if (panel->open)
            panel->onGui(m_context, m_camera);  // SceneView 在其 onGui 内完成整帧 3D 渲染

    handleExternalDrop(m_pendingDrops);
    m_pendingDrops.clear();

    // 选中集含已销毁对象时清理
    std::erase_if(m_context.selectedIds,
                  [this](uint64_t id) { return m_scene->findGameObject(id) == nullptr; });

    ImGui::Render();
}

void EditorApplication::render() {
    // 清默认帧缓冲（避免 docking 半透明背景叠加残影），再绘制全部 ImGui drawlist
    int fbWidth = 0, fbHeight = 0;
    m_window->getFramebufferSize(fbWidth, fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EditorApplication::buildDockLayout(ImGuiID dockspaceId) {
    // 首帧 DockBuilder：左 Hierarchy → 右 Inspector → 底部 Tab（Console + Assets）→ 中央 Scene
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID dockMain = dockspaceId;
    ImGuiID dockLeft = 0, dockRight = 0, dockBottom = 0;
    ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, &dockLeft, &dockMain);
    ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.22f, &dockRight, &dockMain);
    ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, &dockBottom, &dockMain);

    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Console", dockBottom);
    ImGui::DockBuilderDockWindow("Assets", dockBottom);
    ImGui::DockBuilderDockWindow("Scene", dockMain);
    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorApplication::showMainMenu() {
    using namespace NovaEngine;
    const bool editMode = m_context.state == EditorContext::EditorState::Edit;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N", false, editMode)) newScene();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, editMode)) openSceneDialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, editMode)) saveScene();
            if (ImGui::MenuItem("Save Scene As...", nullptr, false, editMode)) saveSceneAs();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem(editMode ? "Play" : "Stop", "Ctrl+P", false, true)) {
                if (editMode) enterPlayMode();
                else exitPlayMode();
            }
            if (ImGui::MenuItem("Pause", nullptr, m_context.paused, !editMode))
                m_context.paused = !m_context.paused;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reload Assets")) AssetDatabase::instance().clearCache();
            ImGui::Separator();
            for (auto& panel : m_panels)
                ImGui::MenuItem(panel->name().c_str(), nullptr, &panel->open);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorApplication::handleGlobalShortcuts() {
    using namespace NovaEngine;
    if (ImGui::GetIO().WantCaptureKeyboard) return;  // 输入框/重命名时抑制快捷键

    const bool ctrl = Input::isKeyDown(Key::LeftControl) || Input::isKeyDown(Key::RightControl);
    if (ctrl && Input::isKeyPressed(Key::P)) {
        if (m_context.state == EditorContext::EditorState::Edit) enterPlayMode();
        else exitPlayMode();
    }
    if (ctrl && Input::isKeyPressed(Key::S)) saveScene();
    if (ctrl && Input::isKeyPressed(Key::N)) newScene();
    if (ctrl && Input::isKeyPressed(Key::O)) openSceneDialog();

    // gizmo 快捷键（W/E/R 切换操作，X 切换坐标空间）
    if (m_context.state == EditorContext::EditorState::Edit) {
        if (Input::isKeyPressed(Key::W)) m_gizmo.setOperation(GizmoController::Operation::Translate);
        if (Input::isKeyPressed(Key::E)) m_gizmo.setOperation(GizmoController::Operation::Rotate);
        if (Input::isKeyPressed(Key::R)) m_gizmo.setOperation(GizmoController::Operation::Scale);
        if (Input::isKeyPressed(Key::X)) m_gizmo.toggleSpace();
    }
}

void EditorApplication::handleExternalDrop(const std::vector<std::filesystem::path>& paths) {
    using namespace NovaEngine;
    if (paths.empty()) return;

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool overViewport = insideRect(mouse, m_context.viewportScreenMin, m_context.viewportScreenMax);
    const bool overBrowser =
        insideRect(mouse, m_context.assetBrowserScreenMin, m_context.assetBrowserScreenMax);

    for (const auto& p : paths) {
        if (overViewport) {
            if (DragDrop::isModelFile(p)) {
                if (m_context.state == EditorContext::EditorState::Play) {
                    Logger::warn("Play 模式下禁止导入模型: {}", p.filename().string());
                    continue;
                }
                // 落点 = 射线与 y=0 平面交点（无则相机 target 前方 3m）
                const glm::vec2 mn(m_context.viewportScreenMin.x, m_context.viewportScreenMin.y);
                const glm::vec2 mx(m_context.viewportScreenMax.x, m_context.viewportScreenMax.y);
                const glm::vec3 spawn = m_camera.intersectGround(
                    glm::vec2(mouse.x, mouse.y), mn, mx - mn);
                if (DragDrop::importModelToScene(*m_scene, p, spawn)) m_context.sceneDirty = true;
            } else if (DragDrop::isTextureFile(p)) {
                DragDrop::copyIntoAssets(p);  // 贴图：仅拷贝进 assets/textures/
            } else {
                Logger::warn("不支持的文件类型（拖到场景视口）: {}", p.filename().string());
            }
        } else if (overBrowser) {
            if (DragDrop::isSceneFile(p)) {
                openScene(p);
            } else if (DragDrop::isModelFile(p) || DragDrop::isTextureFile(p)) {
                DragDrop::copyIntoAssets(p);  // 仅拷贝进 assets/
            } else {
                Logger::warn("不支持的文件类型（拖到资源浏览器）: {}", p.filename().string());
            }
        } else {
            Logger::warn("文件拖放目标无效（请拖到场景视口或资源浏览器）: {}", p.filename().string());
        }
    }
}

void EditorApplication::enterPlayMode() {
    using namespace NovaEngine;
    if (m_context.state != EditorContext::EditorState::Edit) return;

    m_context.playSnapshot = SceneSerializer::serialize(*m_scene);  // 快照 = JSON 字符串（§7.2）
    m_context.state = EditorContext::EditorState::Play;
    m_context.paused = false;
    m_scene->start();  // 深度优先广播 onStart（内置过滤：非激活子树与 disabled Behaviour）
    Logger::info("进入 Play 模式");
}

void EditorApplication::exitPlayMode() {
    using namespace NovaEngine;
    if (m_context.state != EditorContext::EditorState::Play) return;

    // 同一 Scene 对象原地重建（外部持有的 Scene* 不失效；选中集按 ID 天然保留）
    SceneSerializer::deserialize(*m_scene, m_context.playSnapshot);
    m_context.state = EditorContext::EditorState::Edit;
    m_context.paused = false;
    m_context.playSnapshot.clear();
    applyEditorCameraFromScene();
    std::erase_if(m_context.selectedIds,
                  [this](uint64_t id) { return m_scene->findGameObject(id) == nullptr; });
    Logger::info("退出 Play 模式");
}

void EditorApplication::newScene() {
    if (m_context.state != EditorContext::EditorState::Play) {
        m_scene = std::make_unique<NovaEngine::Scene>("Untitled");
        m_context.scene = m_scene.get();
        m_context.currentScenePath.clear();
        m_context.sceneDirty = false;
        m_context.clearSelection();
        applyEditorCameraFromScene();
    }
}

void EditorApplication::openScene(const std::filesystem::path& p) {
    using namespace NovaEngine;
    if (m_context.state == EditorContext::EditorState::Play) {
        Logger::warn("Play 模式下不可加载场景");
        return;
    }
    if (!SceneSerializer::loadFromFile(*m_scene, p)) {
        Logger::error("打开场景失败: {}", p.string());
        return;
    }
    std::error_code ec;
    m_context.currentScenePath = std::filesystem::absolute(p, ec).lexically_normal();
    m_context.sceneDirty = false;
    m_context.clearSelection();
    applyEditorCameraFromScene();  // 编辑器相机位置随场景恢复（§5 editorCamera 段）
    Logger::info("场景已打开: {}", p.string());
}

void EditorApplication::openSceneDialog() {
    if (m_context.state == EditorContext::EditorState::Play) return;
    const auto p = showOpenSceneDialog(NovaEngine::AssetDatabase::projectRoot() / "assets" / "scenes");
    if (!p.empty()) openScene(p);
}

void EditorApplication::saveScene() {
    using namespace NovaEngine;
    if (m_context.state == EditorContext::EditorState::Play) return;  // 保存的是编辑态场景（§7.2）
    if (m_context.currentScenePath.empty()) {
        saveSceneAs();
        return;
    }
    if (SceneSerializer::saveToFile(*m_scene, m_context.currentScenePath)) {
        m_context.sceneDirty = false;
        Logger::info("场景已保存: {}", m_context.currentScenePath.string());
    } else {
        Logger::error("场景保存失败: {}", m_context.currentScenePath.string());
    }
}

void EditorApplication::saveSceneAs() {
    using namespace NovaEngine;
    if (m_context.state == EditorContext::EditorState::Play) return;
    const auto p = showSaveSceneDialog(
        AssetDatabase::projectRoot() / "assets" / "scenes",
        m_context.currentScenePath.empty() ? "untitled.nova.json"
                                           : m_context.currentScenePath.filename().string());
    if (p.empty()) return;
    if (SceneSerializer::saveToFile(*m_scene, p)) {
        m_context.currentScenePath = p;
        m_context.sceneDirty = false;
        Logger::info("场景已保存: {}", p.string());
    } else {
        Logger::error("场景保存失败: {}", p.string());
    }
}

void EditorApplication::applyEditorCameraFromScene() {
    m_camera.target = m_scene->editorCameraTarget;
    m_camera.distance = m_scene->editorCameraDistance;
    m_camera.yaw = m_scene->editorCameraYaw;
    m_camera.pitch = m_scene->editorCameraPitch;
}

}  // namespace NovaEditor
