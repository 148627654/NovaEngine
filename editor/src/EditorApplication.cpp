#include "EditorApplication.h"

#include "DragDrop.h"
#include "Packager.h"
#include "PrefabManager.h"
#include "panels/AssetBrowserPanel.h"
#include "panels/BuildSettingsPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/EnvironmentPanel.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/SceneViewPanel.h"
#include "panels/StatsPanel.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder* API（ImGui 1.90+ 从 imgui.h 移入 internal）
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <glad/gl.h>

#include <NovaEngine/NovaEngine.h>

#include <chrono>
#include <cwchar>
#include <fstream>
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
//（commdlg.h 会拉入 prsht.h，后者依赖 windows.h 里的 CALLBACK 等类型）；
// GLFW 头必须在 glad 之后（glad 的 __gl_h_ 守卫令 GLFW 内嵌的 <GL/gl.h> 变 no-op）。
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>       // SHParseDisplayName / SHCreateShellItem（目录选择对话框初始目录）
#include <shobjidl.h>     // IFileDialog：Common Item Dialog（commdlg 的现代形态，FOS_PICKFOLDERS 选目录）
#include <objbase.h>      // CoInitializeEx / CoCreateInstance
#include <GLFW/glfw3.h>       // updateWindowTitle / run 内窗口关闭拦截用 glfwSetWindowTitle/glfwSetWindowShouldClose
#define GLFW_EXPOSE_NATIVE_WIN32 1  // glfw3native.h 需该宏才声明 glfwGetWin32Window（修复：首编译 C3861）
#include <GLFW/glfw3native.h> // glfwGetWin32Window：目录选择对话框的 owner 句柄

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

// 输出目录选择对话框（IFileDialog = commdlg 的现代形态；FOS_PICKFOLDERS 只选目录）。
// 返回空 = 用户取消。COM 按需初始化（IFileDialog 要求 STA 线程）；owner 为主窗口句柄保持模态。
std::filesystem::path showPickDirectoryDialog(HWND owner, const std::filesystem::path& initialDir) {
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // initHr == RPC_E_CHANGED_MODE 表示本线程已以其他模式初始化 COM，IFileDialog 仍可尝试使用
    std::filesystem::path result;
    IFileDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        if (SUCCEEDED(dlg->GetOptions(&opts)))
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
                            FOS_FORCEFILESYSTEM);
        dlg->SetTitle(L"选择打包输出目录");
        // 初始目录（解析失败静默跳过，对话框退回默认位置）
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(initialDir.c_str(), nullptr, &pidl, 0, nullptr))) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(SHCreateShellItem(nullptr, nullptr, pidl, &psi))) dlg->SetFolder(psi);
            if (psi) psi->Release();
            CoTaskMemFree(pidl);
        }
        if (SUCCEEDED(dlg->Show(owner))) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(dlg->GetResult(&psi))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = std::filesystem::path(std::wstring(path));
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        dlg->Release();
    }
    if (SUCCEEDED(initHr)) CoUninitialize();  // 仅当本函数成功初始化时才反初始化（引用计数配对）
    return result;
}

// 鼠标是否落在面板上报区域内（min==max 视为未上报）
bool insideRect(const ImVec2& p, const ImVec2& mn, const ImVec2& mx) {
    if (mx.x <= mn.x || mx.y <= mn.y) return false;
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y;
}

// §14.2/§15：Play 态每帧把音频监听器同步到「主相机」。选择顺序：
//   AudioListener 组件（第一个 active，§14.2「挂主相机 GO」）→ isMain 相机 →
//   第一个 active 相机 → 编辑器相机位置兜底。
// 与 SceneViewPanel 的 pickPlayCamera（面板内匿名函数，规则一致）解耦实现：
// 监听器更新放在 EditorApplication 而非面板——保证 SceneView 面板关闭时监听器仍更新，
// 且位置落在 §15 帧内顺序的 setListener 位（scene->update 之后、渲染之前）。
void updateAudioListener(NovaEngine::Scene* scene, const NovaEditor::EditorCamera& editorCam) {
    using namespace NovaEngine;
    if (!AudioEngine::available())
        return;   // 无设备降级：跳过（AudioEngine 内部同样 no-op，此处省一次遍历）

    const Transform* t = nullptr;
    for (auto* l : scene->findComponents<AudioListener>())
        if (l->gameObject()->activeInHierarchy()) { t = l->transform(); break; }
    if (!t) {
        auto cameras = scene->findComponents<Camera>();
        for (auto* c : cameras)
            if (c->isMain && c->gameObject()->activeInHierarchy()) { t = c->transform(); break; }
        if (!t)
            for (auto* c : cameras)
                if (c->gameObject()->activeInHierarchy()) { t = c->transform(); break; }
    }

    glm::vec3 pos, forward(0.0f, 0.0f, -1.0f), up(0.0f, 1.0f, 0.0f);
    if (t) {
        pos = t->worldPosition();
        forward = t->forward();   // 世界 forward（Transform 约定 +Z）
        up = t->up();
    } else {
        pos = editorCam.position();
        forward = editorCam.forward();
        up = glm::vec3(0.0f, 1.0f, 0.0f);   // 编辑器相机 up 近似 +Y（俯仰不过极点即成立）
    }
    AudioEngine::setListener(pos, forward, up);
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

    // 五面板 + Stats（§6.5 布局：左 Hierarchy / 右 Inspector / 底 Console+Assets+Stats / 中 Scene）
    auto scenePanel = std::make_unique<SceneViewPanel>();
    scenePanel->m_gizmo = &m_gizmo;
    m_panels.push_back(std::move(scenePanel));
    m_panels.push_back(std::make_unique<HierarchyPanel>());
    m_panels.push_back(std::make_unique<InspectorPanel>());
    m_panels.push_back(std::make_unique<EnvironmentPanel>());  // V0.2-5：dockRight 与 Inspector 并排 Tab
    m_panels.push_back(std::make_unique<ConsolePanel>());
    auto browser = std::make_unique<AssetBrowserPanel>();
    browser->openSceneCallback = [this](const std::filesystem::path& p) { openScene(p); };
    m_panels.push_back(std::move(browser));
    auto statsPanel = std::make_unique<StatsPanel>();
    statsPanel->open = false;   // 默认关，F8 开关（V0.2-11）
    m_statsPanel = statsPanel.get();
    m_panels.push_back(std::move(statsPanel));
    // Build Settings（V0.3-12 波 3，第 8 面板）：默认关，File → Build Settings… 打开
    auto buildSettingsPanel = std::make_unique<BuildSettingsPanel>();
    buildSettingsPanel->open = false;
    m_buildSettingsPanel = buildSettingsPanel.get();
    m_panels.push_back(std::move(buildSettingsPanel));

    // 默认加载示例场景（不存在则保持空场景）
    const std::filesystem::path demo =
        AssetDatabase::projectRoot() / "assets" / "scenes" / "demo.nova.json";
    std::error_code ec;
    if (std::filesystem::exists(demo, ec)) {
        openScene(demo);
    } else {
        Logger::info("未找到示例场景 {}，使用空场景", demo.string());
    }
    // 压 Undo 基线（demo 加载路径内已压，此处兜底统一；clear 语义 = 清空两栈再压基线）
    m_context.undoRedo.clear(SceneSerializer::serialize(*m_scene, -1));

    Logger::info("NovaEditor 初始化完成");
}

void EditorApplication::shutdown() {
    using namespace NovaEngine;
    // 音频引擎最先关闭（V0.3-2）：不依赖 GL，先停掉设备线程与全部声音，
    // 再清面板与 Renderer（§14.2 生命周期）
    AudioEngine::shutdown();
    SceneManager::shutdown();  // V0.3-12 波 3：清活动场景指针/构建列表/待加载队列
    m_panels.clear();  // ConsolePanel 析构时注销 Logger sink
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    SkyboxRenderer::shutdown();  // V0.2-7：与 Renderer 同序清理（GL 上下文仍存活）
    UIRenderer::shutdown();      // V0.3-3：动态 VBO/ui shader 释放（需 GL 上下文存活，先于 Renderer）
    PostProcess::shutdown();     // V0.3-14 波 3：后处理 shader/ping-pong 纹理释放（GL 存活期、Renderer 之前）
    Renderer::shutdown();
    Logger::info("NovaEditor 退出");
    Logger::shutdown();  // 关闭日志文件（逐行已落盘）
}

int EditorApplication::run() {
    using namespace NovaEngine;
    init();
    while (!m_window->shouldClose() && !m_exitRequested) {
        m_window->pollEvents();  // GLFW 事件 → Input 原始状态 + 拖放回调队列
        Time::beginFrame();
        Stats::beginFrame();     // 帧首（V0.2-11：独立于 Time 的 steady_clock 计时）
        Input::beginFrame();
        update(Time::deltaTime());
        render();
        Stats::endFrame();       // 帧末（swapBuffers 前结算入 120 帧环形历史）
        m_window->swapBuffers();

        // 窗口关闭拦截（V0.2-2）：Play 态先退出还原（修复 7.2-4）；判脏且无挂起动作则拦下弹模态
        if (m_window->shouldClose() && !m_exitRequested) {
            if (m_context.state == EditorContext::EditorState::Play) exitPlayMode();
            if (m_context.sceneDirty && m_pendingAction == PendingAction::None) {
                glfwSetWindowShouldClose(m_window->nativeHandle(), GLFW_FALSE);
                m_pendingAction = PendingAction::ExitApp;
                m_showSaveConfirm = true;
            } else if (m_showSaveConfirm) {
                // 模态打开时点窗口 X：视作 Cancel（清挂起动作、关模态、不退出；Reviewer P2-1）
                glfwSetWindowShouldClose(m_window->nativeHandle(), GLFW_FALSE);
                m_showSaveConfirm = false;
                m_pendingAction = PendingAction::None;
            } else {
                m_exitRequested = true;
            }
        }
    }
    shutdown();
    return 0;
}

void EditorApplication::update(float dt) {
    using namespace NovaEngine;

    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    // Docking 主区域；首帧用 DockBuilder 摆面板
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (m_firstFrame) {
        buildDockLayout(dockspaceId);
        m_firstFrame = false;
    }

    // 场景更新段（Stats "update" 分段：scene->update / flushDestroyQueue 前后计时）
    const auto updateT0 = std::chrono::steady_clock::now();
    if (m_context.state == EditorContext::EditorState::Play) {
        if (m_context.paused && m_context.stepRequested) {
            m_context.stepRequested = false;
            m_scene->update(dt);  // V0.3-10 子集：Pause 中单步推进一帧（F10 / Edit→Step）
        } else if (!m_context.paused) {
            m_scene->update(dt);
        }
        // §15 帧内顺序（V0.3-12 波 3）：scene->update 之后执行帧末场景切换
        //（SceneManager::loadScene 入队的请求在此落地；无待加载 no-op）
        SceneManager::flushPendingLoad();
    } else {
        m_scene->flushDestroyQueue();
        // 编辑器相机状态回写场景（随 Ctrl+S 持久化）
        m_scene->editorCameraTarget = m_camera.target;
        m_scene->editorCameraDistance = m_camera.distance;
        m_scene->editorCameraYaw = m_camera.yaw;
        m_scene->editorCameraPitch = m_camera.pitch;
    }
    const auto updateT1 = std::chrono::steady_clock::now();
    Stats::setSectionMs("update",
                        std::chrono::duration<float, std::milli>(updateT1 - updateT0).count());

    // 音频监听器（V0.3-2，§15 帧内顺序）：scene->update 之后、面板渲染（SceneView 整帧 3D
    // 渲染在 onGui 内完成）之前。Edit 态仅 flushDestroyQueue，不进音频路径（退出 Play 已 stopAll）
    if (m_context.state == EditorContext::EditorState::Play)
        updateAudioListener(m_scene.get(), m_camera);

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

    // 未保存确认模态（V0.2-2）：Save 失败不关模态；ESC/X 视作 Cancel
    showSaveConfirmModal();

    updateWindowTitle();  // 每帧：场景名 + 脏标记 *

    ImGui::Render();
}

void EditorApplication::render() {
    // 清默认帧缓冲（避免 docking 半透明背景叠加残影），再绘制全部 ImGui drawlist
    int fbWidth = 0, fbHeight = 0;
    m_window->getFramebufferSize(fbWidth, fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // ImGui 渲染前后计时（Stats "ui" 分段）
    const auto uiT0 = std::chrono::steady_clock::now();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    const auto uiT1 = std::chrono::steady_clock::now();
    NovaEngine::Stats::setSectionMs(
        "ui", std::chrono::duration<float, std::milli>(uiT1 - uiT0).count());
}

void EditorApplication::buildDockLayout(ImGuiID dockspaceId) {
    // 首帧 DockBuilder：左 Hierarchy → 右 Inspector → 底部 Tab（Console + Assets + Stats）→ 中央 Scene
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
    ImGui::DockBuilderDockWindow("Environment", dockRight);  // V0.2-5：与 Inspector 并排 Tab
    ImGui::DockBuilderDockWindow("Console", dockBottom);
    ImGui::DockBuilderDockWindow("Assets", dockBottom);
    ImGui::DockBuilderDockWindow("Stats", dockBottom);
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
            ImGui::Separator();
            // Build Settings（V0.3-12 波 3）：第 8 面板开关（Edit 态可编辑；Play 态经 SceneManager 消费）
            if (ImGui::MenuItem("Build Settings...", nullptr, false, editMode)) {
                if (m_buildSettingsPanel)
                    m_buildSettingsPanel->open = true;
            }
            ImGui::Separator();
            // V0.5-1：一键打包（Play 态禁用；无当前场景路径时点击后提示先保存）
            if (ImGui::MenuItem("Build Player...", nullptr, false, editMode)) buildPlayer();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", nullptr)) requestExit();  // 与窗口关闭同流（判脏弹模态）
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            // Undo/Redo（V0.2-1）：Edit 态 + 栈深度允许 + gizmo 未拖拽时可用
            const bool gizmoFree = !m_gizmo.isUsing();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                                editMode && gizmoFree && m_context.undoRedo.canUndo()))
                undoScene();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
                                editMode && gizmoFree && m_context.undoRedo.canRedo()))
                redoScene();
            ImGui::Separator();
            if (ImGui::MenuItem(editMode ? "Play" : "Stop", "Ctrl+P", false, true)) {
                if (editMode) enterPlayMode();
                else exitPlayMode();
            }
            if (ImGui::MenuItem("Pause", nullptr, m_context.paused, !editMode))
                m_context.paused = !m_context.paused;
            // V0.3-10 子集：仅 Play 且 Paused 时可点，推进一帧
            if (ImGui::MenuItem("Step", "F10", false, !editMode && m_context.paused))
                m_context.stepRequested = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reload Assets")) {
                AssetDatabase::instance().clearCache();
                PrefabManager::clearCache();   // V0.3-9 波 4：预制体源 JSON 缓存一并清空
            }
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
    if (m_showSaveConfirm) return;                   // 未保存确认模态期间抑制全部快捷键

    const bool ctrl = Input::isKeyDown(Key::LeftControl) || Input::isKeyDown(Key::RightControl);
    const bool shift = Input::isKeyDown(Key::LeftShift) || Input::isKeyDown(Key::RightShift);

    if (ctrl && Input::isKeyPressed(Key::P)) {
        if (m_context.state == EditorContext::EditorState::Edit) enterPlayMode();
        else exitPlayMode();
    }
    if (ctrl && Input::isKeyPressed(Key::S)) saveScene();
    if (ctrl && Input::isKeyPressed(Key::N)) newScene();
    if (ctrl && Input::isKeyPressed(Key::O)) openSceneDialog();

    // F8：Stats 面板开关（V0.2-11）
    if (Input::isKeyPressed(Key::F8) && m_statsPanel)
        m_statsPanel->open = !m_statsPanel->open;

    // F10：Play+Paused 单步推进一帧（V0.3-10 子集）
    if (m_context.state == EditorContext::EditorState::Play && m_context.paused &&
        Input::isKeyPressed(Key::F10))
        m_context.stepRequested = true;

    // Undo/Redo（V0.2-1）：仅 Edit 态且 gizmo 未拖拽；Ctrl+Z=undo、Ctrl+Shift+Z/Ctrl+Y=redo
    if (m_context.state == EditorContext::EditorState::Edit && !m_gizmo.isUsing()) {
        if (ctrl && Input::isKeyPressed(Key::Z)) {
            if (shift) redoScene();
            else undoScene();
        }
        if (ctrl && Input::isKeyPressed(Key::Y)) redoScene();
    }

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
    if (m_showSaveConfirm) return;  // 模态期间忽略外部拖放（GLFW 回调不受 ImGui 模态输入拦截，防覆盖挂起动作）

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool overViewport = insideRect(mouse, m_context.viewportScreenMin, m_context.viewportScreenMax);
    const bool overBrowser =
        insideRect(mouse, m_context.assetBrowserScreenMin, m_context.assetBrowserScreenMax);

    Logger::info("外部拖放 {} 个文件，鼠标在 {} 区域（{},{}）", paths.size(),
                 overViewport ? "场景视口" : (overBrowser ? "资源浏览器" : "无效区域"),
                 mouse.x, mouse.y);

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
                if (DragDrop::importModelToScene(*m_scene, p, spawn)) m_context.markSceneDirty();
            } else if (DragDrop::isTextureFile(p)) {
                DragDrop::copyIntoAssets(p);  // 贴图：仅拷贝进 assets/textures/
            } else {
                Logger::warn("不支持的文件类型（拖到场景视口）: {}", p.filename().string());
            }
        } else if (overBrowser) {
            if (DragDrop::isSceneFile(p)) {
                openScene(p);  // 走未保存确认流（V0.2-2）
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

    // 音频生命周期（§14.2）：进入 Play 前停掉一切残留声音（编辑期预览/上一轮 Play 遗留），防爆音
    AudioEngine::stopAll();
    // 场景管理（V0.3-12 波 3）：登记活动场景——Play 中 loadScene/flushPendingLoad 的
    // DDOL 迁移与原地重建都作用于此 Scene 指针（同一对象原地重建，指针不失效）
    SceneManager::setActiveScene(m_scene.get());

    m_context.playSnapshot = SceneSerializer::serialize(*m_scene);  // 快照 = JSON 字符串（§7.2）
    m_context.m_playEntryDirty = m_context.sceneDirty;  // 记录进 Play 前脏状态（修复 7.2-4）
    m_context.state = EditorContext::EditorState::Play;
    m_context.paused = false;
    // V0.3-10 子集：进入 Play 时未保存 → 警告日志 + 非阻塞 3 秒视口 toast
    if (m_context.sceneDirty) {
        Logger::warn("场景存在未保存修改（Play 快照将在退出时还原）");
        m_context.playToastUntil = glfwGetTime() + 3.0;
    }
    m_scene->start();  // 深度优先广播 onStart（内置过滤：非激活子树与 disabled Behaviour）
    Logger::info("进入 Play 模式");
}

void EditorApplication::exitPlayMode() {
    using namespace NovaEngine;
    if (m_context.state != EditorContext::EditorState::Play) return;

    // 音频生命周期（§14.2）：先统一 stopAll 防爆音；随后 deserialize 析构 AudioSource
    // 时各自的析构 stop 已无句柄可停（no-op），语义仍正确
    AudioEngine::stopAll();
    // 场景管理（V0.3-12 波 3）：退出 Play 注销活动场景并清待加载队列
    //（快照还原走 exitPlayMode 自身的 deserialize，与 SceneManager 无关）
    SceneManager::setActiveScene(nullptr);

    // 同一 Scene 对象原地重建（外部持有的 Scene* 不失效；选中集按 ID 天然保留）
    SceneSerializer::deserialize(*m_scene, m_context.playSnapshot);
    m_context.state = EditorContext::EditorState::Edit;
    m_context.paused = false;
    m_context.playSnapshot.clear();
    m_context.playToastUntil = 0.0;                     // toast 仅 Play 态显示，退出即失效
    m_context.sceneDirty = m_context.m_playEntryDirty;  // 还原进 Play 前脏状态（修复 7.2-4）
    applyEditorCameraFromScene();
    std::erase_if(m_context.selectedIds,
                  [this](uint64_t id) { return m_scene->findGameObject(id) == nullptr; });
    Logger::info("退出 Play 模式");
}

void EditorApplication::newScene() {
    // Play 态禁止（首行 return）；判脏 → 挂起 + 弹未保存确认模态
    if (m_context.state == EditorContext::EditorState::Play) return;
    if (m_context.sceneDirty) {
        m_pendingAction = PendingAction::NewScene;
        m_showSaveConfirm = true;
        return;
    }
    newSceneInternal();
}

void EditorApplication::newSceneInternal() {
    using namespace NovaEngine;
    m_scene = std::make_unique<Scene>("Untitled");
    m_context.scene = m_scene.get();
    m_context.currentScenePath.clear();
    m_context.sceneDirty = false;
    m_context.clearSelection();
    applyEditorCameraFromScene();
    m_context.undoRedo.clear(SceneSerializer::serialize(*m_scene, -1));  // 压新基线
    Logger::info("新建场景（Undo 栈已重置）");
}

void EditorApplication::openScene(const std::filesystem::path& p) {
    using namespace NovaEngine;
    if (m_context.state == EditorContext::EditorState::Play) {
        Logger::warn("Play 模式下不可加载场景");
        return;
    }
    if (m_context.sceneDirty) {
        m_pendingAction = PendingAction::OpenScene;
        m_pendingOpenPath = p;
        m_showSaveConfirm = true;
        return;
    }
    loadSceneInternal(p);
}

void EditorApplication::loadSceneInternal(const std::filesystem::path& p) {
    using namespace NovaEngine;
    if (!SceneSerializer::loadFromFile(*m_scene, p)) {
        Logger::error("打开场景失败: {}", p.string());
        return;
    }
    std::error_code ec;
    m_context.currentScenePath = std::filesystem::absolute(p, ec).lexically_normal();
    m_context.sceneDirty = false;
    m_context.clearSelection();
    applyEditorCameraFromScene();  // 编辑器相机位置随场景恢复（§5 editorCamera 段）
    m_context.undoRedo.clear(SceneSerializer::serialize(*m_scene, -1));  // 压新基线
    Logger::info("场景已打开: {}", p.string());
}

void EditorApplication::openSceneDialog() {
    if (m_context.state == EditorContext::EditorState::Play) return;
    const auto p = showOpenSceneDialog(NovaEngine::AssetDatabase::projectRoot() / "assets" / "scenes");
    if (!p.empty()) openScene(p);
}

bool EditorApplication::saveScene() {
    using namespace NovaEngine;
    if (m_context.state == EditorContext::EditorState::Play) return false;  // 保存的是编辑态场景（§7.2）
    if (m_context.currentScenePath.empty()) return saveSceneAs();
    if (SceneSerializer::saveToFile(*m_scene, m_context.currentScenePath)) {
        m_context.sceneDirty = false;
        Logger::info("场景已保存: {}", m_context.currentScenePath.string());
        return true;
    }
    Logger::error("场景保存失败: {}", m_context.currentScenePath.string());
    return false;
}

bool EditorApplication::saveSceneAs() {
    using namespace NovaEngine;
    if (m_context.state == EditorContext::EditorState::Play) return false;
    const auto p = showSaveSceneDialog(
        AssetDatabase::projectRoot() / "assets" / "scenes",
        m_context.currentScenePath.empty() ? "untitled.nova.json"
                                           : m_context.currentScenePath.filename().string());
    if (p.empty()) return false;
    if (SceneSerializer::saveToFile(*m_scene, p)) {
        m_context.currentScenePath = p;
        m_context.sceneDirty = false;
        Logger::info("场景已保存: {}", p.string());
        return true;
    }
    Logger::error("场景保存失败: {}", p.string());
    return false;
}

void EditorApplication::buildPlayer() {
    using namespace NovaEngine;
    if (m_context.state == EditorContext::EditorState::Play) return;  // 菜单已禁用，兜底拦截
    if (m_context.currentScenePath.empty()) {
        Logger::warn("当前场景尚未保存，请先保存场景（File → Save Scene As...）再打包");
        return;
    }
    if (m_context.sceneDirty)
        Logger::warn("场景存在未保存修改，本次将打包上次保存的版本（可先 Ctrl+S 保存最新修改）");

    // 目录选择对话框（默认定位项目根；取消则静默返回）
    const auto outDir = showPickDirectoryDialog(
        glfwGetWin32Window(m_window->nativeHandle()), AssetDatabase::projectRoot());
    if (outDir.empty()) {
        Logger::info("已取消打包");
        return;
    }

    Logger::info("开始打包场景: {} → 输出目录: {}", m_context.currentScenePath.string(),
                 outDir.string());
    if (Packager::buildPlayer(m_context.currentScenePath, outDir))
        Logger::info("Build Player 完成");
    else
        Logger::error("Build Player 失败（详见上方错误清单）");
}

void EditorApplication::showSaveConfirmModal() {
    if (!m_showSaveConfirm) return;

    ImGui::OpenPopup("Unsaved Changes");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    bool stayOpen = true;
    if (ImGui::BeginPopupModal("Unsaved Changes", &stayOpen,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
        ImGui::TextUnformatted("场景有未保存的修改。继续前是否保存？");
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (saveScene()) {  // 保存失败返回 false：不关模态
                m_showSaveConfirm = false;
                processPendingAction();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
            m_showSaveConfirm = false;
            processPendingAction();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_showSaveConfirm = false;
            m_pendingAction = PendingAction::None;
            m_pendingOpenPath.clear();
        }
        ImGui::EndPopup();
    }
    if (!stayOpen && m_showSaveConfirm) {
        // ESC / 右上角 X 关闭：视作 Cancel（不执行挂起动作）
        m_showSaveConfirm = false;
        m_pendingAction = PendingAction::None;
        m_pendingOpenPath.clear();
    }
}

void EditorApplication::processPendingAction() {
    const PendingAction act = m_pendingAction;
    const std::filesystem::path openPath = m_pendingOpenPath;
    m_pendingAction = PendingAction::None;
    m_pendingOpenPath.clear();
    switch (act) {
    case PendingAction::NewScene:  newSceneInternal(); break;
    case PendingAction::OpenScene: loadSceneInternal(openPath); break;
    case PendingAction::ExitApp:   m_exitRequested = true; break;
    case PendingAction::None:      break;
    }
}

void EditorApplication::requestExit() {
    // File→Exit 与窗口关闭同流：Play 先退出还原，判脏弹模态，否则退出
    if (m_context.state == EditorContext::EditorState::Play) exitPlayMode();
    if (m_context.sceneDirty && m_pendingAction == PendingAction::None) {
        m_pendingAction = PendingAction::ExitApp;
        m_showSaveConfirm = true;
        return;
    }
    m_exitRequested = true;
}

void EditorApplication::undoScene() {
    using namespace NovaEngine;
    // 文件恢复通道（V0.3-9 波 4）：undoFull 返回 files = 被撤销操作改写的文件 + 改写前内容
    //（B 线约定：undo 原样写回）；先写回 files 再 deserialize sceneJson
    const auto result = m_context.undoRedo.undoFull();
    if (result.sceneJson.empty()) return;
    for (const auto& fr : result.files) {
        std::ofstream file(AssetDatabase::projectRoot() / fr.path,
                           std::ios::binary | std::ios::trunc);
        if (file) file << fr.content;
        else Logger::warn("Undo 文件恢复失败（无法写入）: {}", fr.path.string());
    }
    PrefabManager::clearCache();   // 资产内容已回写：预制体源缓存失效
    // 恢复 = deserialize 回同一 Scene（对象 ID 不变；选中集仅清理失效 ID）
    SceneSerializer::deserialize(*m_scene, result.sceneJson);
    m_context.sceneDirty = true;
    std::erase_if(m_context.selectedIds,
                  [this](uint64_t id) { return m_scene->findGameObject(id) == nullptr; });
    Logger::info("撤销 Undo（undoDepth={}，redoDepth={}）", m_context.undoRedo.undoDepth(),
                 m_context.undoRedo.redoDepth());
}

void EditorApplication::redoScene() {
    using namespace NovaEngine;
    // redo 文件方向（B 线约定）：files 同为「改写前内容」——消费方按 sceneJson 重算改写后
    // 内容写回（.novaprefab 经 PrefabManager::rewriteAssetFromScene 用场景内实例重序列化；
    // 其它文件原样写回）。先 deserialize 场景（重算依赖场景内实例），再写回文件。
    const auto result = m_context.undoRedo.redoFull();
    if (result.sceneJson.empty()) return;
    SceneSerializer::deserialize(*m_scene, result.sceneJson);
    for (const auto& fr : result.files) {
        if (fr.path.extension() == ".novaprefab" &&
            PrefabManager::rewriteAssetFromScene(m_context, fr.path))
            continue;
        std::ofstream file(AssetDatabase::projectRoot() / fr.path,
                           std::ios::binary | std::ios::trunc);
        if (file) file << fr.content;
        else Logger::warn("Redo 文件恢复失败（无法写入）: {}", fr.path.string());
    }
    PrefabManager::clearCache();
    m_context.sceneDirty = true;
    std::erase_if(m_context.selectedIds,
                  [this](uint64_t id) { return m_scene->findGameObject(id) == nullptr; });
    Logger::info("重做 Redo（undoDepth={}，redoDepth={}）", m_context.undoRedo.undoDepth(),
                 m_context.undoRedo.redoDepth());
}

void EditorApplication::updateWindowTitle() {
    // 每帧：场景名 + (sceneDirty ? " *" : "") + " - NovaEditor"（经 nativeHandle 直调 GLFW）
    const std::string title =
        m_scene->name() + (m_context.sceneDirty ? " *" : "") + " - NovaEditor";
    glfwSetWindowTitle(m_window->nativeHandle(), title.c_str());
}

void EditorApplication::applyEditorCameraFromScene() {
    m_camera.target = m_scene->editorCameraTarget;
    m_camera.distance = m_scene->editorCameraDistance;
    m_camera.yaw = m_scene->editorCameraYaw;
    m_camera.pitch = m_scene->editorCameraPitch;
}

}  // namespace NovaEditor
