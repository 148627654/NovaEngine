#include "EditorApplication.h"

#include "DragDrop.h"
#include "EditorSettings.h"
#include "Packager.h"
#include "PrefabManager.h"
#include "panels/AnimatorPanel.h"
#include "panels/AssetBrowserPanel.h"
#include "panels/BuildSettingsPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/EnvironmentPanel.h"
#include "panels/HierarchyPanel.h"
#include "panels/InputBindingsPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/MixerPanel.h"             // V1.0-8 波 1 合波接线
#include "panels/PhysicsLayersPanel.h"     // V1.0-3 波 2 合波接线
#include "panels/ProfilerPanel.h"          // V1.0-1 波 1 合波接线
#include "panels/ReferenceCheckerPanel.h"  // V1.0-7 波 1 合波接线
#include "panels/ShaderGraphPanel.h"       // V1.0-5 波 2 合波接线
#include "panels/SceneViewPanel.h"
#include "panels/SettingsPanel.h"          // V1.0-12 编辑器设置面板
#include "panels/StatsPanel.h"
#include "panels/TerrainPanel.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder* API（ImGui 1.90+ 从 imgui.h 移入 internal）
#include <ImGuizmo.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <glad/gl.h>

#include <NovaEngine/NovaEngine.h>
#include <NovaEngine/Scripting/ScriptRuntime.h>

#include <cstdlib>   // std::system（脚本热重载的构建 spawn）

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
    // V1.0-7 波 1 合波接线：AssetBrowser 右键 Reference Checker 入口（运行时查找面板名）
    browser->openReferenceChecker = [this]() {
        for (auto& p : m_panels)
            if (p->name() == "Reference Checker") { p->open = true; return; }
    };
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
    // Input Bindings（V0.5-11，第 9 面板）：默认关，File → Input Bindings… 打开；dock 底部 Tab
    auto inputBindingsPanel = std::make_unique<InputBindingsPanel>();
    inputBindingsPanel->open = false;
    m_inputBindingsPanel = inputBindingsPanel.get();
    m_panels.push_back(std::move(inputBindingsPanel));
    // Terrain（V0.5-8 波 2 2b 合波接线，第 10 面板）：默认关，View 菜单打开；dock 底部 Tab
    auto terrainPanel = std::make_unique<TerrainPanel>();
    terrainPanel->open = false;
    m_panels.push_back(std::move(terrainPanel));
    // Animator（V0.5-9 波 2 2b 合波接线，第 11 面板）：默认开（验收反馈：默认关导致
    // 用户选中带 Animator 的对象后"看不到面板"——改为启动即显示，View 菜单仍可关）
    auto animatorPanel = std::make_unique<AnimatorPanel>();
    animatorPanel->open = true;
    m_panels.push_back(std::move(animatorPanel));
    // Profiler（V1.0-1 波 1 合波接线，第 12 面板）：默认关，Window → Profiler 打开；dock 底部 Tab
    auto profilerPanel = std::make_unique<ProfilerPanel>();
    profilerPanel->open = false;
    m_panels.push_back(std::move(profilerPanel));
    // Reference Checker（V1.0-7 波 1 合波接线，第 13 面板）：默认关，Assets 菜单/AssetBrowser 右键打开
    auto referenceCheckerPanel = std::make_unique<ReferenceCheckerPanel>();
    referenceCheckerPanel->open = false;
    m_panels.push_back(std::move(referenceCheckerPanel));
    // Mixer（V1.0-8 波 1 合波接线，第 14 面板）：默认关，Audio → Mixer… 打开
    auto mixerPanel = std::make_unique<MixerPanel>();
    mixerPanel->open = false;
    m_panels.push_back(std::move(mixerPanel));
    // Physics Layers（V1.0-3 波 2 合波接线，第 15 面板）：默认关，View 菜单打开；dock 底部 Tab
    auto physicsLayersPanel = std::make_unique<PhysicsLayersPanel>();
    physicsLayersPanel->open = false;
    m_panels.push_back(std::move(physicsLayersPanel));
    // Shader Graph（V1.0-5 波 2 合波接线，第 16 面板）：默认关，View 菜单/AssetBrowser 右键打开
    auto shaderGraphPanel = std::make_unique<ShaderGraphPanel>();
    shaderGraphPanel->open = false;
    m_panels.push_back(std::move(shaderGraphPanel));
    // Settings（V1.0-12，第 17 面板）：默认关，Edit → Editor Settings… 打开；dock 底部 Tab
    auto settingsPanel = std::make_unique<SettingsPanel>();
    settingsPanel->open = false;
    m_settingsPanel = settingsPanel.get();
    m_panels.push_back(std::move(settingsPanel));

    // 编辑器设置（V1.0-12，§28.2）：main 已在 ImGui 配置期 load（多视口标志）；
    // 此处把其余数据应用到运行态（snap/主题/样式/最近文件）。
    // 注意顺序：必须早于默认场景加载——loadSceneInternal 会登记最近文件，
    // 后应用会覆盖掉刚登记的条目
    applyEditorSettings();

    // 脚本 DLL 初始加载（V0.5-7 波 2 2b 合波修复：此前漏接，编辑器启动即加载——
    // 必须在场景加载之前，脚本组件反序列化才能命中注册表；失败仅 warn，场景其余正常）
#ifdef NOVA_SCRIPTS_DLL
    ScriptRuntime::setDllPath(NOVA_GAME_DLL_PATH);
    if (!ScriptRuntime::load())
        Logger::warn("[ScriptRuntime] NovaGame.dll 加载失败（脚本组件将跳过；先编译 samples）: {}",
                     ScriptRuntime::lastError());
#endif

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

    // 输入绑定（V0.5-11）：项目根/assets/input/default.novainput。失败仅 warn 不阻断
    //（InputMap::load 内部已记 warn，内置默认绑定兜底；可经 File → Input Bindings… 配置）
    InputMap::load(AssetDatabase::projectRoot() / "assets" / "input" / "default.novainput");

    Logger::info("NovaEditor 初始化完成");
}

void EditorApplication::applyEditorSettings() {
    using namespace NovaEngine;
    EditorSettings& settings = EditorSettings::instance();
    const EditorSettings::Data& data = settings.data;

    // 吸附值 → Snap Settings 运行态（Inspector 面板同源读写）
    m_context.snapSettings.position = glm::vec3(data.snapMove);
    m_context.snapSettings.rotationDeg = data.snapRotate;
    m_context.snapSettings.scale = data.snapScale;

    // 主题 + ImGuiStyle 自定义覆盖（ImGui 上下文已建）
    EditorSettings::applyTheme(data.theme);
    EditorSettings::applyStyleCustom(data);

    // 最近文件（加载时已做存在性过滤；此处转换为运行态路径列表）
    m_context.recentFiles.clear();
    for (const std::string& s : data.recentFiles)
        if (m_context.recentFiles.size() < 10)
            m_context.recentFiles.emplace_back(std::filesystem::path(s));

    Logger::info("编辑器设置已应用（snap {}/{}/{}，主题 {}，最近文件 {} 个）", data.snapMove,
                 data.snapRotate, data.snapScale, data.theme, m_context.recentFiles.size());
}

void EditorApplication::syncEditorSettings() {
    EditorSettings& settings = EditorSettings::instance();
    EditorSettings::Data& data = settings.data;

    // 吸附值（Inspector Snap Settings 拖拽每帧变化 → 差异即 dirty）
    if (data.snapMove != m_context.snapSettings.position.x ||
        data.snapRotate != m_context.snapSettings.rotationDeg ||
        data.snapScale != m_context.snapSettings.scale) {
        data.snapMove = m_context.snapSettings.position.x;
        data.snapRotate = m_context.snapSettings.rotationDeg;
        data.snapScale = m_context.snapSettings.scale;
        settings.markDirty();
    }

    // 最近文件（场景打开/另存后 ctx 更新 → 回写）
    bool recentDiff = data.recentFiles.size() != m_context.recentFiles.size();
    if (!recentDiff) {
        for (size_t i = 0; i < m_context.recentFiles.size(); ++i)
            if (data.recentFiles[i] != m_context.recentFiles[i].string()) { recentDiff = true; break; }
    }
    if (recentDiff) {
        data.recentFiles.clear();
        for (const auto& p : m_context.recentFiles) data.recentFiles.push_back(p.string());
        settings.markDirty();
    }
}

void EditorApplication::maybeSaveEditorSettings() {
    using namespace NovaEngine;
    EditorSettings& settings = EditorSettings::instance();
    if (!settings.dirty()) return;
    m_settingsSaveCooldown -= Time::deltaTime();
    if (m_settingsSaveCooldown > 0.0) return;   // 节流：拖拽等高频变更合并写盘
    settings.save();
    settings.clearDirty();
    m_settingsSaveCooldown = 0.5;   // 0.5s 节流窗口
}

void EditorApplication::shutdown() {
    using namespace NovaEngine;
    // 音频引擎最先关闭（V0.3-2）：不依赖 GL，先停掉设备线程与全部声音，
    // 再清面板与 Renderer（§14.2 生命周期）
    AudioEngine::shutdown();
    SceneManager::shutdown();  // V0.3-12 波 3：清活动场景指针/构建列表/待加载队列
    AsyncLoader::shutdown();   // V1.0-2 波 2 合波接线（§27.1：须先于 Renderer::shutdown——join 加载线程）
    m_panels.clear();  // ConsolePanel 析构时注销 Logger sink
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    SkyboxRenderer::shutdown();  // V0.2-7：与 Renderer 同序清理（GL 上下文仍存活）
    UIRenderer::shutdown();      // V0.3-3：动态 VBO/ui shader 释放（需 GL 上下文存活，先于 Renderer）
    PostProcess::shutdown();     // V0.3-14 波 3：后处理 shader/ping-pong 纹理释放（GL 存活期、Renderer 之前）
    IBL::shutdown();             // V0.5-4 波 2 合波：卷积缓存纹理/BRDF LUT 释放（GL 存活期）
    SSAO::shutdown();            // V0.5-5 波 2 合波：AO 纹理链释放（GL 存活期）
    ParticleRenderer::shutdown();  // V0.5-6 波 2 合波：粒子 VAO/VBO/贴图缓存释放（GL 存活期）
    TerrainRenderer::shutdown();  // V0.5-8 波 2 合波补：地形 VAO/VBO/阴影网格缓存释放（GL 存活期；
                                  // 缺失时静态缓存内的 shared_ptr<Mesh> 在上下文死后析构 → 退出崩溃，
                                  // 2026-11-19 harness 复现）
    Renderer::shutdown();
    // 编辑器设置强制落盘（V1.0-12：节流窗口内退出也不丢变更）
    syncEditorSettings();
    if (EditorSettings::instance().dirty()) EditorSettings::instance().save();
    Logger::info("NovaEditor 退出");
    Logger::shutdown();  // 关闭日志文件（逐行已落盘）
}

int EditorApplication::run() {
    using namespace NovaEngine;
    init();
    if (m_quitAfterInit) {   // V0.5-2 CLI --quit：init 完成后立即退出（CI 冒烟）
        shutdown();
        return 0;
    }
    while (!m_window->shouldClose() && !m_exitRequested) {
        m_window->pollEvents();  // GLFW 事件 → Input 原始状态 + 拖放回调队列
        Time::beginFrame();
        Stats::beginFrame();     // 帧首（V0.2-11：独立于 Time 的 steady_clock 计时）
        Profiler::beginFrame();  // V1.0-1 波 1 合波接线（§26.2：帧首，Stats::beginFrame 之后）
        Input::beginFrame();
        InputMap::beginFrame();  // V0.5-11：每帧一次，紧跟 Input::beginFrame 之后（契约 §24.5）
        update(Time::deltaTime());
        render();
        Stats::endFrame();       // 帧末（swapBuffers 前结算入 120 帧环形历史）
        Profiler::endFrame();    // V1.0-1 波 1 合波接线（§26.2：帧末，swapBuffers 前）
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
    // ImGuizmo 标准用法（README）：每帧 BeginFrame 一次——内部复位
    // mbOverGizmoHotspot 等逐帧状态（该状态无 BeginFrame 时一旦悬停过手柄
    // 即永久粘死 → HandleTranslation/Scale/Rotation 的激活路径恒短路，
    // 2026-11-17 用户报障「拖箭头没反应」，[GizmoDiag] 定位）；
    // 每视口随后 SetDrawlist + SetRect + Manipulate（SceneViewPanel 内已按此调用）。
    ImGuizmo::BeginFrame();

    // HiDPI（2026-11-16）：每帧同步显示器内容缩放（SCALE_TO_MONITOR 下
    // ImGui FramebufferScale 恒 1，UI 逻辑尺寸倍率需独立来源）
    {
        float csx = 1.0f, csy = 1.0f;
        glfwGetWindowContentScale(m_window->nativeHandle(), &csx, &csy);
        m_context.displayContentScale = std::max(csx, 1.0f);
    }

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
            SceneManager::updateAll(dt);  // V1.0-2 波 2 合波（§27.1）：全部已加载场景（active 先）；
                                          // V0.3-10 子集：Pause 中单步推进一帧（F10 / Edit→Step）
        } else if (!m_context.paused) {
            SceneManager::updateAll(dt);  // V1.0-2（§27.1）：替代单 m_scene->update
        }
        // §15 修订帧序（V1.0-2 波 2 合波接线，§27.1）：flushPendingResults（GPU 上传+场景挂载）
        // 必须先于 flushPendingLoad（挂载引用已上传资产）；两者都在 update 之后执行
        AsyncLoader::flushPendingResults();
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
    Profiler::pushSection("update",
                          std::chrono::duration<float, std::milli>(updateT1 - updateT0).count());  // V1.0-1 波 1 合波接线（§26.2 模块档）

    // 音频监听器（V0.3-2，§15 帧内顺序）：scene->update 之后、面板渲染（SceneView 整帧 3D
    // 渲染在 onGui 内完成）之前。Edit 态仅 flushDestroyQueue，不进音频路径（退出 Play 已 stopAll）
    if (m_context.state == EditorContext::EditorState::Play)
        updateAudioListener(m_scene.get(), m_camera);

#ifdef NOVA_SCRIPTS_DLL
    pollScriptDll();   // 脚本 DLL mtime 轮询（V0.5-7；Edit 触发 / Play 记变化）
#endif

    showMainMenu();
    handleGlobalShortcuts();

    // 面板区域上报每帧先清零（未渲染的面板不残留旧区域）
    m_context.viewportScreenMin = m_context.viewportScreenMax = ImVec2(0.0f, 0.0f);
    m_context.assetBrowserScreenMin = m_context.assetBrowserScreenMax = ImVec2(0.0f, 0.0f);
    const auto renderT0 = std::chrono::steady_clock::now();
    for (auto& panel : m_panels)
        if (panel->open)
            panel->onGui(m_context, m_camera);  // SceneView 在其 onGui 内完成整帧 3D 渲染
    // V1.0-1 波 1 合波接线（§26.2 模块档）：SceneView 的 3D 渲染在面板循环内完成，
    // 其余面板 onGui 仅构建 ImGui drawlist（开销可忽略），该段即渲染段
    Profiler::pushSection(
        "render", std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - renderT0).count());

    handleExternalDrop(m_pendingDrops);
    m_pendingDrops.clear();

    // 选中集含已销毁对象时清理
    std::erase_if(m_context.selectedIds,
                  [this](uint64_t id) { return m_scene->findGameObject(id) == nullptr; });

    // 未保存确认模态（V0.2-2）：Save 失败不关模态；ESC/X 视作 Cancel
    showSaveConfirmModal();
    // 存档槽模态（V0.5-12）：Play 态 Save Game / Load Game
    showSaveGameModal();
    showLoadGameModal();

    updateWindowTitle();  // 每帧：场景名 + 脏标记 *

    // 编辑器设置同步（V1.0-12）：运行态差异 → 数据 + dirty；节流落盘
    syncEditorSettings();
    maybeSaveEditorSettings();

    ImGui::Render();
    // 多视口（V1.0-12）：ImGui::Render 不自动 UpdatePlatformWindows（1.93），
    // 平台窗口（拖出的面板）需主循环显式渲染；RenderPlatformWindowsDefault 内部
    // 切换各视口 GL 上下文，包一层备份恢复与 main 视图继续自有 GL 调用互不干扰。
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }
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
    NovaEngine::Profiler::pushSection(
        "ui", std::chrono::duration<float, std::milli>(uiT1 - uiT0).count());  // V1.0-1 波 1 合波接线（§26.2 模块档）
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
    ImGui::DockBuilderDockWindow("Input Bindings", dockBottom);  // V0.5-11：底部 Tab
    ImGui::DockBuilderDockWindow("Terrain", dockBottom);         // V0.5-8：底部 Tab
    ImGui::DockBuilderDockWindow("Animator", dockBottom);        // V0.5-9：底部 Tab
    ImGui::DockBuilderDockWindow("Profiler", dockBottom);          // V1.0-1 波 1 合波接线
    ImGui::DockBuilderDockWindow("Reference Checker", dockBottom); // V1.0-7 波 1 合波接线
    ImGui::DockBuilderDockWindow("Mixer", dockBottom);             // V1.0-8 波 1 合波接线
    ImGui::DockBuilderDockWindow("Physics Layers", dockBottom);    // V1.0-3 波 2 合波接线
    ImGui::DockBuilderDockWindow("Shader Graph", dockBottom);      // V1.0-5 波 2 合波接线
    ImGui::DockBuilderDockWindow("Settings", dockBottom);          // V1.0-12 编辑器设置
    ImGui::DockBuilderDockWindow("Scene", dockMain);
    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorApplication::showMainMenu() {
    using namespace NovaEngine;
    const bool editMode = m_context.state == EditorContext::EditorState::Edit;
    // 按面板名查找并打开（V1.0 波 1 合波接线：Window/Assets/Audio 菜单入口）
    auto openPanelByName = [this](const char* name) {
        for (auto& panel : m_panels)
            if (panel->name() == name) { panel->open = true; return; }
    };

    if (ImGui::BeginMainMenuBar()) {
        // 快捷键提示按当前绑定显示（V1.0-12；未重绑时与硬编码文案一致）
        const std::string kbNew = EditorSettings::chordFor("New Scene");
        const std::string kbOpen = EditorSettings::chordFor("Open Scene");
        const std::string kbSave = EditorSettings::chordFor("Save Scene");
        const std::string kbUndo = EditorSettings::chordFor("Undo");
        const std::string kbRedo = EditorSettings::chordFor("Redo");
        const std::string kbPlay = EditorSettings::chordFor("Play/Stop");
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", kbNew.c_str(), false, editMode)) newScene();
            // 2D 模板（V0.5-10 波 2 2b 合波接线）：从 assets/scenes/2DTemplate.nova.json 创建
            // （与 Open Scene 同流：判脏确认；打开后 Ctrl+S 会存回模板路径——与打开任意资产场景一致）
            if (ImGui::MenuItem("New 2D Scene", nullptr, false, editMode))
                openScene(std::filesystem::path("assets/scenes/2DTemplate.nova.json"));
            if (ImGui::MenuItem("Open Scene...", kbOpen.c_str(), false, editMode)) openSceneDialog();
            // 最近文件（V1.0-12，§28.2）：≤10，显示时做文件存在性过滤——被删除文件不列出；
            // 菜单构建后文件被删的竞态由 loadSceneInternal 的失败路径兜底（不崩溃）
            if (ImGui::BeginMenu("Open Recent", editMode)) {
                bool anyListed = false;
                for (const auto& r : m_context.recentFiles) {
                    std::error_code ec;
                    if (!std::filesystem::exists(r, ec)) continue;   // 存在性过滤
                    anyListed = true;
                    if (ImGui::MenuItem(r.filename().string().c_str(), r.string().c_str()))
                        openScene(r);
                }
                if (!anyListed) ImGui::MenuItem("(空)", nullptr, false, false);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene", kbSave.c_str(), false, editMode)) saveScene();
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
            // 存档（V0.5-12）：仅 Play 态可用（Save/Load 作用于运行期场景）
            // 打开时缓存一次槽列表（模态期间不每帧扫盘刷日志）
            if (ImGui::MenuItem("Save Game", nullptr, false, !editMode)) {
                m_saveGameSlots = SaveGame::listSlots();
                m_showSaveGameModal = true;
            }
            if (ImGui::MenuItem("Load Game", nullptr, false, !editMode)) {
                m_saveGameSlots = SaveGame::listSlots();
                m_showLoadGameModal = true;
            }
            ImGui::Separator();
            // 输入绑定面板（V0.5-11）：仅 Edit 态可用（Play 中绑定改动即时生效但配置编辑约定在 Edit）
            if (ImGui::MenuItem("Input Bindings...", nullptr, false, editMode))
                if (m_inputBindingsPanel) m_inputBindingsPanel->open = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", nullptr)) requestExit();  // 与窗口关闭同流（判脏弹模态）
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            // Undo/Redo（V0.2-1）：Edit 态 + 栈深度允许 + gizmo 未拖拽时可用
            const bool gizmoFree = !m_gizmo.isUsing();
            if (ImGui::MenuItem("Undo", kbUndo.c_str(), false,
                                editMode && gizmoFree && m_context.undoRedo.canUndo()))
                undoScene();
            if (ImGui::MenuItem("Redo", kbRedo.c_str(), false,
                                editMode && gizmoFree && m_context.undoRedo.canRedo()))
                redoScene();
            ImGui::Separator();
            if (ImGui::MenuItem(editMode ? "Play" : "Stop", kbPlay.c_str(), false, true)) {
                if (editMode) enterPlayMode();
                else exitPlayMode();
            }
            if (ImGui::MenuItem("Pause", nullptr, m_context.paused, !editMode))
                m_context.paused = !m_context.paused;
            // V0.3-10 子集：仅 Play 且 Paused 时可点，推进一帧
            if (ImGui::MenuItem("Step", EditorSettings::chordFor("Step").c_str(), false,
                                !editMode && m_context.paused))
                m_context.stepRequested = true;
            ImGui::Separator();
            // 编辑器设置面板（V1.0-12）：主题/快捷键/多视口（吸附值在 Inspector Snap Settings）
            if (ImGui::MenuItem("Editor Settings...", nullptr, false, editMode))
                if (m_settingsPanel) m_settingsPanel->open = true;
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
        // ===== V1.0 波 1 合波接线：Window/Assets/Audio 菜单 =====
        //（三个面板同时出现在 View 自动列表与底部 dock Tab，菜单为显式打开入口）
        if (ImGui::BeginMenu("Window")) {
            if (ImGui::MenuItem("Profiler")) openPanelByName("Profiler");
            // V1.0 波 2 面板入口（2026-11-24 用户反馈补：面板存在但关闭后无入口可再打开——
            // 合波日 dock 接线漏了 Window 菜单项）
            if (ImGui::MenuItem("Shader Graph")) openPanelByName("Shader Graph");
            if (ImGui::MenuItem("Physics Layers")) openPanelByName("Physics Layers");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Assets")) {
            if (ImGui::MenuItem("Reference Checker...")) openPanelByName("Reference Checker");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Audio")) {
            if (ImGui::MenuItem("Mixer...")) openPanelByName("Mixer");
            ImGui::EndMenu();
        }
        // 脚本热重载（V0.5-7 波 2 2b 合波接线）：Edit 态可用；Play 态灰显（快照含旧类型）
#ifdef NOVA_SCRIPTS_DLL
        if (ImGui::BeginMenu("Scripts")) {
            // P2-5：快捷键提示走设置表（与其它菜单 chordFor 显示一致）
            if (ImGui::MenuItem("Reload Scripts",
                                EditorSettings::chordFor("Reload Scripts").c_str(), false,
                                editMode && ScriptRuntime::isLoaded()))
                reloadScriptsFlow();
            if (ImGui::MenuItem("Auto Reload", nullptr, &m_autoReloadScripts)) {}
            ImGui::EndMenu();
        }
#endif
        ImGui::EndMainMenuBar();
    }
}

void EditorApplication::handleGlobalShortcuts() {
    using namespace NovaEngine;
    if (ImGui::GetIO().WantCaptureKeyboard) return;  // 输入框/重命名时抑制快捷键
    if (m_showSaveConfirm) return;                   // 未保存确认模态期间抑制全部快捷键

    // V1.0-12（§28.2）：核心快捷键走设置表（chordFor 恒有值——默认表兜底；
    // 未重绑时与既有硬编码行为逐键一致）
    const bool editMode = m_context.state == EditorContext::EditorState::Edit;

    if (EditorSettings::chordMatches(EditorSettings::chordFor("Play/Stop"))) {
        if (editMode) enterPlayMode();
        else exitPlayMode();
    }
    if (EditorSettings::chordMatches(EditorSettings::chordFor("Save Scene"))) saveScene();
    if (EditorSettings::chordMatches(EditorSettings::chordFor("New Scene"))) newScene();
    if (EditorSettings::chordMatches(EditorSettings::chordFor("Open Scene"))) openSceneDialog();
#ifdef NOVA_SCRIPTS_DLL
    // 脚本热重载（V0.5-7；仅 Edit 态——Play 快照含旧类型）
    if (EditorSettings::chordMatches(EditorSettings::chordFor("Reload Scripts")) && editMode)
        reloadScriptsFlow();
#endif
    // 单步推进一帧（V0.3-10 子集；Play+Paused）
    if (EditorSettings::chordMatches(EditorSettings::chordFor("Step")) &&
        m_context.state == EditorContext::EditorState::Play && m_context.paused)
        m_context.stepRequested = true;

    // F8：Stats 面板开关（V0.2-11；编辑器工具键，不进核心设置表）
    if (Input::isKeyPressed(Key::F8) && m_statsPanel)
        m_statsPanel->open = !m_statsPanel->open;

    // Undo/Redo（V0.2-1）：仅 Edit 态且 gizmo 未拖拽
    if (editMode && !m_gizmo.isUsing()) {
        if (EditorSettings::chordMatches(EditorSettings::chordFor("Undo"))) undoScene();
        if (EditorSettings::chordMatches(EditorSettings::chordFor("Redo"))) redoScene();
        // 兼容别名 Ctrl+Shift+Z = Redo（仅默认绑定未重绑时保留，与旧行为一致）
        if (EditorSettings::chordFor("Undo") == "Ctrl+Z" &&
            EditorSettings::chordFor("Redo") == "Ctrl+Y") {
            const bool ctrl =
                Input::isKeyDown(Key::LeftControl) || Input::isKeyDown(Key::RightControl);
            const bool shift =
                Input::isKeyDown(Key::LeftShift) || Input::isKeyDown(Key::RightShift);
            if (ctrl && shift && Input::isKeyPressed(Key::Z)) redoScene();
        }
    }

    // gizmo 快捷键（W/E/R 切换操作，X 切换坐标空间；编辑器工具键，不进设置表）
    if (editMode) {
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
    AsyncLoader::reset();   // V1.0-2 波 2 合波接线（§27.1）：清队列+代际递增，旧异步结果作废
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
    m_showSaveGameModal = false;                        // 存档模态仅 Play 态：退出即关闭
    m_showLoadGameModal = false;
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
    m_context.addRecentFile(p);    // 最近文件登记（V1.0-12；去重/上限由 addRecentFile 保证）
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
        m_context.addRecentFile(p);  // 另存场景同样进最近文件（V1.0-12）
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

void EditorApplication::showSaveGameModal() {
    using namespace NovaEngine;
    if (!m_showSaveGameModal) return;

    ImGui::OpenPopup("Save Game");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    bool stayOpen = true;
    if (ImGui::BeginPopupModal("Save Game", &stayOpen,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
        ImGui::TextUnformatted("选择存档槽位（1-8）：");
        ImGui::Separator();
        // 槽元数据（打开模态时缓存一次：浅校验合法槽带 valid=true + scene/时间戳/游戏时间）
        const std::vector<SaveSlotInfo>& slots = m_saveGameSlots;
        for (int slot = 1; slot <= 8; ++slot) {
            const SaveSlotInfo* info = nullptr;
            for (const SaveSlotInfo& s : slots)
                if (s.slot == slot) { info = &s; break; }
            const std::string label = [&]() -> std::string {
                if (info && info->valid)
                    return "槽 " + std::to_string(slot) + "：" +
                           (info->scenePath.empty() ? std::string("(未知场景)")
                                                    : info->scenePath) +
                           "（" + std::to_string(static_cast<int>(info->playTimeSeconds)) + " 秒）";
                if (info && !info->valid) return "槽 " + std::to_string(slot) + "：（损坏/版本不支持）";
                return "槽 " + std::to_string(slot) + "：（空）";
            }();
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 72.0f);
            ImGui::PushID(slot);
            if (ImGui::Button("Save", ImVec2(72.0f, 0.0f))) {
                // 覆盖式保存（原子写）；playTime 编辑器侧不跟踪传 0
                if (SaveGame::save(*m_scene, slot, m_context.currentScenePath.string())) {
                    Logger::info("已保存到槽 {}", slot);
                    m_showSaveGameModal = false;
                }
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("关闭")) m_showSaveGameModal = false;
        ImGui::EndPopup();
    }
    if (!stayOpen) m_showSaveGameModal = false;   // ESC / 右上角 X：关闭模态
}

void EditorApplication::showLoadGameModal() {
    using namespace NovaEngine;
    if (!m_showLoadGameModal) return;

    ImGui::OpenPopup("Load Game");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    bool stayOpen = true;
    if (ImGui::BeginPopupModal("Load Game", &stayOpen,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
        ImGui::TextUnformatted("选择存档槽位读取（1-8）：");
        ImGui::Separator();
        const std::vector<SaveSlotInfo>& slots = m_saveGameSlots;   // 打开模态时缓存一次
        for (int slot = 1; slot <= 8; ++slot) {
            const SaveSlotInfo* info = nullptr;
            for (const SaveSlotInfo& s : slots)
                if (s.slot == slot) { info = &s; break; }
            const bool valid = info != nullptr && info->valid;
            const std::string label = [&]() -> std::string {
                if (valid)
                    return "槽 " + std::to_string(slot) + "：" +
                           (info->scenePath.empty() ? std::string("(未知场景)")
                                                    : info->scenePath) +
                           "（" + std::to_string(static_cast<int>(info->playTimeSeconds)) + " 秒）";
                if (info && !info->valid) return "槽 " + std::to_string(slot) + "：（损坏/版本不支持）";
                return "槽 " + std::to_string(slot) + "：（空）";
            }();
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 72.0f);
            ImGui::PushID(slot);
            if (!valid) ImGui::BeginDisabled();   // 空/损坏槽：Load 置灰
            if (ImGui::Button("Load", ImVec2(72.0f, 0.0f))) {
                // 读档原地重建同一 Scene（对象 ID 保持）；失败 valid=false 且场景不变
                const SaveSlotInfo result = SaveGame::load(*m_scene, slot);
                if (result.valid) {
                    // 清理失效选中 ID（对象 ID 保持，正常情况下选中集全部有效）
                    std::erase_if(m_context.selectedIds,
                                  [this](uint64_t id) { return m_scene->findGameObject(id) == nullptr; });
                    Logger::info("已从槽 {} 读取存档", slot);
                    m_showLoadGameModal = false;
                } else {
                    Logger::warn("槽 {} 读取失败（文件缺失/损坏/版本不支持），场景保持不变", slot);
                }
            }
            if (!valid) ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("关闭")) m_showLoadGameModal = false;
        ImGui::EndPopup();
    }
    if (!stayOpen) m_showLoadGameModal = false;   // ESC / 右上角 X：关闭模态
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

#ifdef NOVA_SCRIPTS_DLL
// ===== 脚本 DLL 热重载（V0.5-7 波 2 2b 合波接线；契约触发流程 §25.5）=====

namespace {
// NovaGame.dll 的 mtime（秒粒度；文件缺失/IO 失败 → 0 = 未初始化语义）
long long dllMtimeOrZero() {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(NOVA_GAME_DLL_PATH, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}
}  // namespace

void EditorApplication::reloadScriptsFlow() {
    using namespace NovaEngine;
    if (m_context.state != EditorContext::EditorState::Edit) return;   // Play 态严禁（快照含旧类型）
    if (!ScriptRuntime::isLoaded()) {
        Logger::warn("[ScriptReload] NovaGame.dll 未加载（先编译 samples 或等待首次构建），跳过");
        return;
    }
    // 1. spawn 构建并等待退出；失败 → 构建输出进 Console、旧 DLL 保持运行、不调 reload
    const std::string cmd = "cmake --build \"" NOVA_PROJECT_ROOT
                            "/build\" --config " NOVA_BUILD_CONFIG " --target NovaGame";
    Logger::info("[ScriptReload] 构建 NovaGame…");
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        Logger::error("[ScriptReload] NovaGame 构建失败（退出码 {}），旧 DLL 保持运行", rc);
        m_lastDllMtime = dllMtimeOrZero();   // 记录当前 mtime，防轮询反复重试
        return;
    }
    // 2. 重载（成功/失败日志已含重载脚本数与字段恢复数或 lastError）
    if (!ScriptRuntime::reload(*m_scene)) {
        Logger::error("[ScriptReload] 热重载失败：{}", ScriptRuntime::lastError());
        m_lastDllMtime = dllMtimeOrZero();
        return;
    }
    Logger::info("[ScriptReload] 热重载完成（脚本字段按对象 ID 原位恢复）");
    m_lastDllMtime = dllMtimeOrZero();
    // 整场景 JSON 重建后全部 GO/组件指针作废；选中集以 ID 存储，每帧对已销毁 ID 清理——
    // 有效 ID 按新实例自然恢复，无需额外重解析
}

void EditorApplication::pollScriptDll() {
    using namespace NovaEngine;
    if (!m_autoReloadScripts) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastDllPoll < std::chrono::seconds(1)) return;   // 1s 节流
    m_lastDllPoll = now;

    const long long mtime = dllMtimeOrZero();
    if (mtime == 0) return;                        // DLL 未编译（首启）——不触发
    if (m_lastDllMtime == 0) {                     // 首次观察：仅记录，不触发
        m_lastDllMtime = mtime;
        return;
    }
    if (m_context.state == EditorContext::EditorState::Play) {
        if (mtime != m_lastDllMtime) {             // Play 中跳过重载，退出后补触发一次
            m_dllChangedInPlay = true;
            m_lastDllMtime = mtime;
        }
        return;
    }
    if (m_dllChangedInPlay) {                      // 退出 Play 后的补触发
        m_dllChangedInPlay = false;
        reloadScriptsFlow();
        return;
    }
    if (mtime != m_lastDllMtime) reloadScriptsFlow();
}
#endif   // NOVA_SCRIPTS_DLL

}  // namespace NovaEditor
