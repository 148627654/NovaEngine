// main.cpp —— NovaEditor 入口（§3.1 主循环的前置初始化步骤 + §24.5.1 CLI 批处理）
//
// 初始化顺序（严格照 §3.1）：
//   Window::create → current_path(项目根) → Renderer::init → AssetDatabase::setProjectRoot
//   → ImGui 上下文 + docking + 样式 + 中文字体
//   → ImGui_ImplGlfw_InitForOpenGL / ImGui_ImplOpenGL3_Init
//   → EditorApplication.run()（内部 init/主循环/shutdown）
//
// CLI（V0.5-2，契约 §24.5.1）：
//   NovaEditor.exe                                # 无参数 = 原 GUI 模式，行为零变化
//   NovaEditor.exe --headless [--import] [--build --scene <path> --out <dir>] [--quit]
//   --headless：Window::create 后立即 glfwHideWindow（保留 GL 上下文——Renderer::init 依赖），
//               跳过 ImGui/面板/EditorApplication，任务完成即退出（等价隐含 --quit）
//   --import：扫描 assets/ 模型/贴图全量导入生成缓存；失败数 M>0 → 退出码 1
//   --build：复用 Packager::buildPlayer；--scene 缺省 assets/scenes/demo.nova.json，
//            --out 缺省 <项目根>/dist
//   --quit：GUI 启动即退（CI 冒烟）
//   退出码：0 成功；1 任务失败；2 参数错误。stderr + Logs 文件双通道记错误。

#include <NovaEngine/NovaEngine.h>

#include "EditorApplication.h"
#include "Packager.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

// windows.h 必须先于 glfw3.h（minwindef 无条件 #define APIENTRY，后包含会 C4005 宏重定义）
#include <windows.h>  // SetConsoleOutputCP：控制台切 UTF-8，修复中文日志乱码

#include <GLFW/glfw3.h>  // glfwGetWindowContentScale（HiDPI 字体缩放）/ glfwHideWindow（headless）

#include <algorithm>   // std::max / std::sort / std::find
#include <cctype>      // std::tolower（扩展名小写化）
#include <filesystem>
#include <iostream>    // std::cerr（参数错误/任务失败 stderr 通道）
#include <memory>
#include <string>
#include <vector>

// 编译期注入的项目根路径（editor/CMakeLists.txt）；未定义时回退当前目录
#ifndef NOVA_PROJECT_ROOT
#define NOVA_PROJECT_ROOT "."
#endif

namespace {

// 命令行参数（§24.5.1）
struct CliOptions {
    bool headless = false;
    bool importAssets = false;
    bool build = false;
    bool quit = false;
    std::string scene = "assets/scenes/demo.nova.json";  // --scene 缺省
    std::string outDir;                                   // --out 缺省 <项目根>/dist
};

// 参数解析：未知参数/缺值 → stderr 提示并返回 false（退出码 2）
bool parseCli(int argc, char** argv, CliOptions& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") {
            out.headless = true;
        } else if (arg == "--import") {
            out.importAssets = true;
        } else if (arg == "--build") {
            out.build = true;
        } else if (arg == "--quit") {
            out.quit = true;
        } else if (arg == "--scene" || arg == "--out") {
            if (i + 1 >= argc) {
                std::cerr << "NovaEditor: 参数 " << arg << " 缺少值" << std::endl;
                return false;
            }
            const std::string value = argv[++i];
            if (arg == "--scene") out.scene = value;
            else out.outDir = value;
        } else {
            std::cerr << "NovaEditor: 未知参数: " << arg << std::endl;
            return false;
        }
    }
    return true;
}

// --import（§24.5.1）：递归扫描 assets/ 下全部模型（.obj/.fbx/.gltf/.glb/.dae）与贴图
//（.png/.jpg/.jpeg/.bmp/.tga/.hdr），逐资产 importModel/getTexture 生成二进制缓存。
// 日志汇总「成功 N / 失败 M」；返回 0 成功 / 1 有失败。
int runImportTask() {
    using namespace NovaEngine;
    AssetDatabase& db = AssetDatabase::instance();
    const std::filesystem::path assetsDir = AssetDatabase::projectRoot() / "assets";
    std::error_code ec;
    if (!std::filesystem::is_directory(assetsDir, ec)) {
        Logger::error("导入失败：assets/ 目录不存在: {}", assetsDir.string());
        std::cerr << "NovaEditor: 导入失败：assets/ 目录不存在" << std::endl;
        return 1;
    }

    static const std::vector<std::string> kModelExts = {".obj", ".fbx", ".gltf", ".glb", ".dae"};
    static const std::vector<std::string> kTextureExts = {".png", ".jpg", ".jpeg", ".bmp",
                                                          ".tga", ".hdr"};
    std::vector<std::filesystem::path> models, textures;
    for (std::filesystem::recursive_directory_iterator it(
             assetsDir, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) {
            // 跳过隐藏目录（.novacache 二进制缓存等），不递归
            const std::string dirName = it->path().filename().string();
            if (!dirName.empty() && dirName.front() == '.') it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) { ec.clear(); continue; }
        const std::string name = it->path().filename().string();
        if (!name.empty() && name.front() == '.') continue;   // 隐藏文件
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (std::find(kModelExts.begin(), kModelExts.end(), ext) != kModelExts.end())
            models.push_back(it->path());
        else if (std::find(kTextureExts.begin(), kTextureExts.end(), ext) != kTextureExts.end())
            textures.push_back(it->path());
    }
    if (ec) Logger::warn("遍历 assets/ 中断: {}", ec.message());

    // 确定性顺序（同输入同日志顺序）
    std::sort(models.begin(), models.end());
    std::sort(textures.begin(), textures.end());

    int ok = 0, fail = 0;
    for (const auto& abs : models) {
        const auto rel = std::filesystem::relative(abs, AssetDatabase::projectRoot());
        const AssetImportResult result = db.importModel(rel.generic_string());
        if (result.meshes.empty()) {
            ++fail;
            Logger::error("模型导入失败: {}", rel.generic_string());
        } else {
            ++ok;
            Logger::info("模型导入成功: {}（{} 个子网格）", rel.generic_string(),
                         result.meshes.size());
        }
    }
    for (const auto& abs : textures) {
        const auto rel = std::filesystem::relative(abs, AssetDatabase::projectRoot());
        if (db.getTexture(rel.generic_string())) {
            ++ok;
        } else {
            ++fail;
            Logger::error("贴图导入失败: {}", rel.generic_string());
        }
    }
    Logger::info("全量导入完成：成功 {} / 失败 {}", ok, fail);
    if (fail > 0) {
        std::cerr << "NovaEditor: 全量导入有 " << fail << " 个失败（成功 " << ok << "）"
                  << std::endl;
        return 1;
    }
    return 0;
}

// --build（§24.5.1）：复用 Packager::buildPlayer（无 UI 依赖）。返回 0 成功 / 1 失败
int runBuildTask(const CliOptions& opt) {
    using namespace NovaEngine;
    // 输出目录：--out 相对路径按项目根解析（与 --scene 同规则）
    const std::filesystem::path outPath(opt.outDir);
    const std::filesystem::path out = opt.outDir.empty()
        ? AssetDatabase::projectRoot() / "dist"
        : (outPath.is_absolute() ? outPath : AssetDatabase::projectRoot() / outPath);
    Logger::info("命令行打包：scene={} out={}", opt.scene, out.string());
    if (NovaEditor::Packager::buildPlayer(opt.scene, out)) return 0;
    std::cerr << "NovaEditor: 打包失败（详见日志）" << std::endl;
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace NovaEngine;

    // 程序输出 UTF-8（源码 /utf-8 编译），但中文 Windows 控制台默认 GBK（代码页 936）解码，
    // 不切换会导致中文日志乱码
    SetConsoleOutputCP(CP_UTF8);

    // 0.5 文件日志提前 init：参数错误（退出码 2）也能双通道留痕（§24.5.1）。
    //     NOVA_PROJECT_ROOT 为编译期绝对路径，不依赖后文 current_path
    Logger::initFileLog(std::filesystem::path(NOVA_PROJECT_ROOT) / "Logs", "NovaEditor");

    // 0. 命令行解析（参数错误 → stderr + 文件日志 + 退出码 2，§24.5.1）
    CliOptions cli;
    if (!parseCli(argc, argv, cli)) {
        Logger::error("命令行参数错误（详见 stderr），退出码 2");
        return 2;
    }

    Logger::info("NovaEditor 启动{}", cli.headless ? "（headless 批处理）" : "");

    // 1. 窗口
    auto window = Window::create("NovaEditor", 1600, 900);
    if (!window) {
        Logger::error("窗口创建失败（GLFW 初始化错误）");
        // headless 依赖隐藏窗口的 GL 上下文：无 GPU 环境创建失败 → 退出码 2（§24.5.1 备案）
        return cli.headless ? 2 : 1;
    }
    window->setVsync(true);
    // headless（V0.5-2）：创建后立即隐藏窗口——保留 GL 上下文（Renderer::init 依赖），
    // 后续跳过 ImGui/面板/EditorApplication，任务完成即退出
    if (cli.headless) glfwHideWindow(window->nativeHandle());

    // 2. 工作目录切到编译期注入的项目根——必须在 Renderer::init 之前：
    //    渲染器按相对路径加载 assets/shaders（双击 exe 时 cwd 是 exe 目录会找不到）
    std::filesystem::current_path(NOVA_PROJECT_ROOT);

    // 3. 渲染器（必须在窗口上下文创建之后；headless 下上下文存活，照常初始化）
    Renderer::init();
    SkyboxRenderer::init();  // V0.2-7：天空渲染（空 VAO + skybox shader；失败仅报错不中断）
    UIRenderer::init();      // V0.3-3：运行时 UI（ui.vert/.frag + 动态 VBO；失败降级 no-op）
    PostProcess::init();     // V0.3-14 波 3：后处理 shader + 全屏 VAO（失败 → 强制 disabled 降级）

    // 4. 项目根（与第 2 步的 current_path 一致；VS 调试运行目录同样指向项目根）
    AssetDatabase::setProjectRoot(std::filesystem::current_path());
    Logger::info("项目根目录: {}", AssetDatabase::projectRoot().string());

    // 4.3 崩溃兜底（2026-11-16：terminate/SEH 时把原因写进日志再走默认处理；
    //    文件日志已在参数解析前 init，此处不再重复调用——重复调用会换新文件）
    Logger::installCrashHandlers();

    // 4.5 场景管理（V0.3-12 波 3）：读盘 assets/build.novabuild（Build Settings 列表 =
    // Play 中 SceneManager::loadScene 的候选集）。必须在 setProjectRoot 之后（AssetStream
    // 按项目根解析）；缺失/损坏 → 空列表 + warn，不崩溃
    SceneManager::init();

    // 5. headless 分支（V0.5-2）：批处理任务 → GL 清理 → 退出（不触碰 ImGui/音频/EditorApplication）
    if (cli.headless) {
        int code = 0;
        if (cli.importAssets) code = runImportTask();
        if (code == 0 && cli.build) code = runBuildTask(cli);   // 导入失败即不再打包
        // GL 资源清理（窗口析构最后释放上下文）
        SceneManager::shutdown();
        UIRenderer::shutdown();
        PostProcess::shutdown();
        SkyboxRenderer::shutdown();
        Renderer::shutdown();
        Logger::info("NovaEditor headless 完成（退出码 {}）", code);
        Logger::shutdown();
        return code;
    }

    // 5.1 命令行任务（--import/--build 不带 --headless）：先执行任务再进入 GUI
    //（与 Unity -batchmode 行为对齐，v1 允许；失败仅告警不阻断 GUI）
    {
        int taskCode = 0;
        if (cli.importAssets) taskCode = runImportTask();
        if (taskCode == 0 && cli.build) taskCode = runBuildTask(cli);
        if (taskCode != 0)
            Logger::warn("命令行任务失败（退出码 {}），继续进入 GUI（-batchmode 对齐）", taskCode);
    }

    // 5.5 音频引擎（V0.3-2 §14.2：编辑器启动即 init，Play 只 stopAll）。
    // 无可用设备时 init 返回 false——音频功能降级禁用（available()=false 全 API no-op），
    // 不中断编辑器；失败原因已由 AudioEngine::init 内部 Logger::warn 记录。
    if (!AudioEngine::init())
        Logger::warn("音频引擎初始化失败：音频功能已禁用（不影响其余功能）");

    // 6. ImGui 上下文 + docking + 样式 + 中文字体
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // ViewportsEnable 默认关（§11）
    ImGui::StyleColorsDark();

    // 默认字体 + 合并中文字体（msyh.ttc 不存在时回退纯默认字体，§11.9）
    // ImGui 1.93 新字体图集：目标字体显式基准字号 16px，合并字体同样显式 16px 才合法
    //（隐式字号目标 + 显式字号合并会触发 imgui_draw.cpp 的 MergeMode 断言）
    // HiDPI（2026-11-16）：窗口按逻辑尺寸创建、帧缓冲随显示器缩放——字体按
    // 内容缩放比放大烘焙（150% 显示器 = 24px 物理），否则 ImGui 面板文字在
    // 高分屏上只有 2/3 大小。clamp ≥1 防未就绪时返回 0。
    float contentScaleX = 1.0f, contentScaleY = 1.0f;
    glfwGetWindowContentScale(window->nativeHandle(), &contentScaleX, &contentScaleY);
    const float fontPx = 16.0f * std::max(1.0f, contentScaleX);
    ImFontConfig baseCfg{};
    baseCfg.SizePixels = fontPx;
    io.Fonts->AddFontDefault(&baseCfg);
    ImFontConfig fontCfg{};
    fontCfg.MergeMode = true;  // 合并进默认字体图集，避免双字体切换
    if (!io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", fontPx, &fontCfg,
                                      io.Fonts->GetGlyphRangesChineseFull()))
        Logger::warn("中文字体加载失败（C:/Windows/Fonts/msyh.ttc），回退默认字体");

    // 6.5 ImGui 后端（GLFW 回调会链式调用 Window 已安装的回调，Input 不受影响）
    if (!ImGui_ImplGlfw_InitForOpenGL(window->nativeHandle(), true)) {
        Logger::error("ImGui GLFW 后端初始化失败");
        return 1;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 450")) {
        Logger::error("ImGui OpenGL3 后端初始化失败");
        return 1;
    }

    // 7. 应用主循环；run() 结束时内部调用 shutdown() 清理 ImGui 与 Renderer，
    //    窗口由 EditorApplication 析构最后释放（GL 上下文随窗口销毁）。
    NovaEditor::EditorApplication app(std::move(window));
    if (cli.quit) app.setQuitAfterInit(true);   // --quit：GUI 启动即退（CI 冒烟）
    return app.run();
}
