// main.cpp —— NovaPlayer 入口（§10.5：打包运行时播放器，最小主循环，无 ImGui）
//
// V1.0-11 窗口元数据（ROADMAP §7-11）：标题取 NOVA_PRODUCT（player/CMakeLists.txt
// 编译期注入，默认 "NovaEngine"）；未定义时回退 "NovaPlayer"。
//
// 初始化顺序：SetConsoleOutputCP(UTF8) → 解析参数 → Window::create → vsync
//   → Renderer::init + SkyboxRenderer::init → AssetDatabase::setProjectRoot(exe 目录)
//   → AssetStream::mountPak（默认 exe 同目录第一个 *.novapak，可 --pak 显式指定）
//   → Scene + SceneSerializer::loadFromFile → scene.start()
//   → 主循环（pollEvents/Time/Input → scene.update(dt) → SceneRenderer::renderScene
//     → swapBuffers；Esc 退出）
//   → shutdown 清理（SkyboxRenderer → Renderer → 窗口析构 glfwTerminate）
//
// 参数：--scene（V1.0-11 §27.5：缺省不写死——解析顺序 = --scene 显式 >
//         pak 内 build.novabuild 第一个场景 > error + 退出码非 0）
//       --pak（默认自动扫描 exe 同目录 *.novapak）--width/--height（默认 1280x720）

#include <windows.h>   // SetConsoleOutputCP + GetModuleFileNameW（须在 glad 之前）

#include <NovaEngine/NovaEngine.h>
#ifdef NOVA_SCRIPTS_DLL
#include <NovaEngine/Scripting/ScriptRuntime.h>   // V0.5-7：脚本 DLL 热重载运行时（exe 目录/NovaGame.dll）
#endif

#include <glad/gl.h>                    // glViewport
#include <GLFW/glfw3.h>                  // glfwSetWindowAttrib（固定窗口；GLFW_INCLUDE_NONE 已定义）
#include <glm/gtc/matrix_transform.hpp> // lookAt / perspective / radians

#include <chrono>      // V1.0-1 波 1 合波接线：Profiler 模块档计时
#include <cstring>
#include <filesystem>
#include <string>

namespace {

// 窗口标题（V1.0-11）：NOVA_PRODUCT 编译期注入（player/CMakeLists.txt）；回退 NovaPlayer
#ifndef NOVA_PRODUCT
#define NOVA_PRODUCT "NovaPlayer"
#endif

struct PlayerArgs {
    std::filesystem::path scene;   // 空 = 自动取 build.novabuild 第一个场景（§27.5）
    std::filesystem::path pak;     // 空 = 自动扫描 exe 目录
    int width = 1280;
    int height = 720;
};

bool parseInt(const char* s, int& out) {
    try {
        size_t consumed = 0;
        const long v = std::stol(s, &consumed);
        if (consumed != std::strlen(s) || v <= 0 || v > 16384) return false;
        out = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

PlayerArgs parseArgs(int argc, char** argv) {
    PlayerArgs args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--scene" && hasValue) {
            args.scene = argv[++i];
        } else if (arg == "--pak" && hasValue) {
            args.pak = argv[++i];
        } else if (arg == "--width" && hasValue) {
            parseInt(argv[++i], args.width);
        } else if (arg == "--height" && hasValue) {
            parseInt(argv[++i], args.height);
        } else {
            NovaEngine::Logger::warn("[NovaPlayer] 未知参数: {}", arg);
        }
    }
    return args;
}

std::filesystem::path executableDir() {
    wchar_t buf[32768]{};
    const DWORD len = GetModuleFileNameW(nullptr, buf, 32767u);
    if (len == 0 || len >= 32767u) return std::filesystem::current_path();
    return std::filesystem::path(buf).parent_path();
}

// 场景主相机（isMain 优先 → 第一个 Camera；跳过非激活子树）；无相机返回 nullptr。
// 与 SceneRenderer::collectCamera 同一选取约定（§7.3）。
const NovaEngine::Camera* findMainCamera(const NovaEngine::Scene& scene) {
    const NovaEngine::Camera* first = nullptr;
    const NovaEngine::Camera* main = nullptr;
    scene.forEachObject([&](NovaEngine::GameObject* go) {
        if (!go->activeInHierarchy()) return;
        const NovaEngine::Camera* cam = go->getComponent<NovaEngine::Camera>();
        if (!cam) return;
        if (!first) first = cam;
        if (cam->isMain && !main) main = cam;
    });
    return main ? main : first;
}

// 默认 pak（V0.5-1 / V1.0-11 §27.5）：exe 同目录优先 NovaEngine.pak
//（buildRelease 发布约定，单 pak 多场景）；其次第一个 *.novapak（buildPlayer
// 单场景约定，§10.5）——发行目录内两约定并存时发布包优先，顺序确定
std::filesystem::path findPakNextToExe(const std::filesystem::path& exeDir) {
    std::error_code ec;
    const std::filesystem::path releasePak = exeDir / "NovaEngine.pak";
    if (std::filesystem::is_regular_file(releasePak, ec)) return releasePak;
    ec.clear();
    for (const auto& entry : std::filesystem::directory_iterator(exeDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() == ".novapak") return entry.path();
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    using namespace NovaEngine;

    // 源码 /utf-8 编译，中文 Windows 控制台默认 GBK → 切 UTF-8 防中文日志乱码
    SetConsoleOutputCP(CP_UTF8);

    const PlayerArgs args = parseArgs(argc, argv);
    const std::filesystem::path exeDir = executableDir();
    Logger::info("[NovaPlayer] 启动（exe 目录: {}）", exeDir.string());

    // 1. 窗口（先于 Renderer::init：GL 上下文）
    auto window = Window::create(NOVA_PRODUCT, args.width, args.height);
    // 波 3 #10：示例游戏按 1280×720 设计基准布局——固定窗口尺寸，规避最大化后
    // GLFW_SCALE_TO_MONITOR 下鼠标坐标 × fb/win 双重换算导致的 UI 命中错位
    //（KNOWN-ISSUES 备案，波 3 #12 编辑器细节统一修复 resize/DPI 映射）
    if (window)
        glfwSetWindowAttrib(window->nativeHandle(), GLFW_RESIZABLE, GLFW_FALSE);
    if (!window) {
        Logger::error("[NovaPlayer] 窗口创建失败（GLFW 初始化错误）");
        return 1;
    }
    window->setVsync(true);

    // 2. 项目根 = exe 目录（AssetDatabase 相对路径锚点；AssetStream pak 降维锚点）
    AssetDatabase::setProjectRoot(exeDir);

    // 2.5 文件日志 + 崩溃兜底（2026-11-16：<项目根>/Logs/NovaPlayer_<时间戳>.log；
    //     terminate/SEH 时把原因写进日志文件再走默认处理）
    Logger::installCrashHandlers();
    Logger::initFileLog(exeDir / "Logs", "NovaPlayer");

    // 3. pak 挂载（默认 exe 同目录 *.novapak；存在才挂载）。
    //    须先于 Renderer::init：Renderer/SkyboxRenderer 初始化即经 AssetStream 加载 shader
    std::filesystem::path pakPath = args.pak;
    if (pakPath.empty()) pakPath = findPakNextToExe(exeDir);
    if (!pakPath.empty()) {
        if (std::filesystem::exists(pakPath))
            AssetStream::mountPak(pakPath);
        else
            Logger::error("[NovaPlayer] pak 不存在: {}", pakPath.string());
    }

    // 4. 渲染器 + 天空（须在窗口上下文创建之后；shader 经 AssetStream：
    //    pak 挂载后走内存，无 pak 时相对工作目录——VS 调试运行目录已指向项目根）
    Renderer::init();
    SkyboxRenderer::init();
    AsyncLoader::init();   // V1.0-2 波 2 合波接线（§27.1：Renderer::init 之后启动加载线程）

    // 5. 音频 + 运行时 UI + 后处理（V0.3-2/3/14）：音频失败只 warn 继续（优雅降级）；
    //   UI 失败内部 no-op；后处理失败内部降级为直通
    if (!AudioEngine::init())
        Logger::warn("[NovaPlayer] 音频设备初始化失败，继续无音频运行");
    // V1.0-8 波 1 合波接线（§26.5）：读总线设置（pak 内缺失自动生成默认；音频未初始化时 no-op）
    AudioEngine::loadSettings("assets/audio/audio-settings.novaaudio");
    UIRenderer::init();
    PostProcess::init();

    // 6. 场景管理（V0.3-12 波 3）：读 build 列表（缺失/损坏 → 空列表 + warn，不崩溃）
    SceneManager::init();

#ifdef NOVA_SCRIPTS_DLL
    // 6.5 脚本 DLL（V0.5-7）：exe 目录/NovaGame.dll（Packager 拷贝入发布目录）。
    //   失败仅 warn——场景加载时脚本组件反序列化 warn 跳过，场景其余正常（§25.5）
    ScriptRuntime::setDllPath(exeDir / "NovaGame.dll");
    if (!ScriptRuntime::load())
        Logger::warn("[NovaPlayer] 脚本 DLL 加载失败，脚本组件将跳过（{}）", ScriptRuntime::lastError());
#endif

    // 7. 场景解析（V1.0-11 §27.5 默认启动场景）：--scene 显式 > build.novabuild
    //    第一个场景（SceneManager::init 已读盘，pak 内命中优先）> error + 退出码非 0
    std::filesystem::path scenePath = args.scene;
    if (scenePath.empty()) {
        const auto& build = SceneManager::buildList();
        if (!build.empty()) scenePath = build.front().path;
    }
    if (scenePath.empty()) {
        Logger::error("[NovaPlayer] 未指定启动场景且 build.novabuild 场景列表为空"
                      "（--scene <path> 可显式指定）");
        return 1;
    }

    // 7.5 场景加载（AssetStream：先查 pak 再查磁盘）+ Play 启动
    Scene scene("Player");
    if (!SceneSerializer::loadFromFile(scene, scenePath)) {
        Logger::error("[NovaPlayer] 场景加载失败: {}", scenePath.string());
        return 1;
    }
    // 输入绑定（V0.5-11）：场景加载后经 AssetStream 读 pak 内 assets/input/default.novainput；
    // 失败仅 warn（内置默认绑定兜底），不阻断启动
    InputMap::load("assets/input/default.novainput");
    scene.start();
    // 注册为运行期场景并设为活动场景（§14.9；LoadScene 延迟切换经 flushPendingLoad 作用于本 scene）
    SceneManager::registerScene(scene.name(), scenePath);
    SceneManager::setActiveScene(&scene);

    // 8. 主循环（无相机时 fallback 相机兜底；场景相机经 collectCamera 优先）
    RenderCamera fallback;
    fallback.position = glm::vec3(0.0f, 1.5f, 8.0f);
    fallback.view = glm::lookAt(fallback.position, glm::vec3(0.0f, 1.0f, 0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f));
    fallback.projection = glm::mat4(1.0f);

    // V0.3-14 后处理链 FBO：hdrFbo（RGBA16F/RGBA8，4x MSAA）+ outLdrFbo（RGBA8 最终画面，无 MSAA）。
    // 格式随 pp.enabled && hdrSupported 决定；运行期 pp 开关翻转时销毁重建（Player 无设置 UI，正常不变）
    Framebuffer hdrFbo, outLdrFbo;
    bool fbosCreated = false;
    bool hdrFboIsHdr = false;
    bool hdrFboNormalDepth = false;   // 波 2 SSAO MRT 附件开关（变化 → 全删重建）

    while (!window->shouldClose()) {
        window->pollEvents();
        Time::beginFrame();
        Input::beginFrame();
        InputMap::beginFrame();   // 每帧一次，紧跟 Input::beginFrame 之后（契约 §24.5）
        Profiler::beginFrame();   // V1.0-1 波 1 合波接线（§26.2：帧首）

        if (Input::isKeyPressed(Key::Escape)) break;

        // 视口跟随帧缓冲（分辨率自适应）；fallback 投影供场景相机 aspect 推导
        int fbW = args.width, fbH = args.height;
        window->getFramebufferSize(fbW, fbH);
        const float aspect = fbH > 0 ? static_cast<float>(fbW) / static_cast<float>(fbH)
                                     : 16.0f / 9.0f;
        fallback.projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        glViewport(0, 0, fbW, fbH);

        // UI 输入（V0.3-3 契约：Scene::update 之前调用 → 脚本 onUpdate 读到本帧按钮状态）。
        // HiDPI（2026-11-16）：UI 全局缩放 = 显示器内容缩放（150% = 1.5）——GLFW
        // SCALE_TO_MONITOR 下窗口/帧缓冲同倍放大，比值恒 1，须用内容缩放才能
        // 把 fontSize 等逻辑尺寸映射到物理像素（文字按 1.5 倍物理分辨率渲染不发虚）
        // V1.0-12 §28.2.1：鼠标换算收敛——窗口逻辑 → 布局空间统一走
        // UIRenderer::layoutFromWindowPoint（内容缩放参与）；processInput 期望物理 FBO
        // 坐标 → 布局 ×contentScale 还原物理。固定 1280×720 + SCALE_TO_MONITOR 下
        // contentScale == fb/win（恒等），与旧公式逐位等价（评审 P2-5 语义不变；
        // 旧 winW/winH>0 守卫在固定窗口下不可达，随公式一并收敛）。
        float csx = 1.0f, csy = 1.0f;
        window->getContentScale(csx, csy);
        const float contentScale = std::max(csx, 1.0f);
        UIRenderer::setDpiScale(contentScale);
        glm::vec2 uiMouse =
            UIRenderer::layoutFromWindowPoint(Input::mousePosition(), contentScale);
        uiMouse *= contentScale;   // 布局 → 物理 FBO（processInput 契约：内部 ÷dpiScale）
        UIRenderer::processInput(scene, uiMouse, Input::isMouseDown(MouseButton::Left),
                                 Input::isMousePressed(MouseButton::Left));

        // 帧序（§15 修订版，V1.0-2 波 2 合波接线 §27.1）：updateAll（全部已加载场景，
        // 物理→脚本→协程→事件）→ AsyncLoader::flushPendingResults（GPU 上传+场景挂载）
        // → SceneManager 延迟切场景 → 音频监听 → 3D+后处理+UI → 显示
        const auto updateT0 = std::chrono::steady_clock::now();
        SceneManager::updateAll(Time::deltaTime());
        AsyncLoader::flushPendingResults();   // §27.1：必须先于 flushPendingLoad（挂载引用已上传资产）
        SceneManager::flushPendingLoad();     // §15：update 之后调用（延迟执行防广播中迭代器失效）
        Profiler::pushSection(   // V1.0-1 波 1 合波接线（§26.2 模块档）
            "update", std::chrono::duration<float, std::milli>(
                          std::chrono::steady_clock::now() - updateT0).count());

        // 音频监听跟随场景主相机（V0.3-2；直接用主相机 Transform，§14.2）
        if (const Camera* cam = findMainCamera(scene); cam && cam->transform()) {
            const Transform* t = cam->transform();
            AudioEngine::setListener(t->worldPosition(), t->forward(), t->up());
        } else {
            // 无场景相机：fallback 相机（view 为 lookAt 矩阵，forward = -第三行，up = 第二行）
            const glm::vec3 fwd(-fallback.view[2][0], -fallback.view[2][1], -fallback.view[2][2]);
            const glm::vec3 up(fallback.view[1][0], fallback.view[1][1], fallback.view[1][2]);
            AudioEngine::setListener(fallback.position, fwd, up);
        }

        // V0.3-14：完整渲染链（场景→描边/线框注入→resolve→后处理→UI），输出到 outLdrFbo
        // 波 2 合波（§25.3 + 骨架日落地裁定 4）：SSAO 开且 hdr 时挂法线/线性深度附件（MRT）
        const bool hdr = scene.postProcess.enabled && PostProcess::hdrSupported();
        // V1.0-4 波 2 合波（cG 遗留 3）：DOF 需线性深度附件（与 SceneViewPanel 同款条件）
        const bool normalDepth = hdr && (scene.postProcess.ssaoEnabled || scene.postProcess.dofEnabled);
        if (!fbosCreated || hdr != hdrFboIsHdr || normalDepth != hdrFboNormalDepth) {
            hdrFbo.destroy();
            outLdrFbo.destroy();
            hdrFbo.create(fbW, fbH, 4, hdr, normalDepth);
            outLdrFbo.create(fbW, fbH, 0, false);
            hdrFboIsHdr = hdr;
            hdrFboNormalDepth = normalDepth;
            fbosCreated = true;
        } else {
            hdrFbo.resize(fbW, fbH);
            outLdrFbo.resize(fbW, fbH);
        }
        const auto renderT0 = std::chrono::steady_clock::now();
        if (fbosCreated && hdrFbo.width() > 0 && outLdrFbo.width() > 0) {
            // V1.0-2 波 2 合波接线（§27.1）：多场景合并渲染——loadedScenes()（active 在前）；
            // 单场景 = 与旧入口逐像素等价（§27.7-1）
            const auto& loadedScenes = SceneManager::loadedScenes();
            std::vector<const Scene*> scenes(loadedScenes.begin(), loadedScenes.end());
            SceneRenderer::renderScenesToOutput(scenes, fallback, hdrFbo,
                                                outLdrFbo, scene.postProcess);
            // renderSceneToOutput 已 unbind（回到默认帧缓冲）——UI 已在其内部绘制于 outLdrFbo，
            // 此处经 PostProcess::apply 的 disabled 直通 blit 把最终画面拷回默认帧缓冲（免 FBO 句柄泄漏）
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fbW, fbH);
            PostProcess::apply(outLdrFbo.colorTextureId(), PostProcessSettings{});
        } else {
            // FBO 创建失败兜底：旧直通路径（渲染到默认帧缓冲）
            SceneRenderer::renderScene(scene, fallback);
            UIRenderer::render(scene, fbW, fbH);
        }
        Profiler::pushSection(   // V1.0-1 波 1 合波接线（§26.2 模块档）
            "render", std::chrono::duration<float, std::milli>(
                          std::chrono::steady_clock::now() - renderT0).count());
        Profiler::endFrame();    // V1.0-1 波 1 合波接线（§26.2：帧末，swapBuffers 前）
        window->swapBuffers();
    }

    // 8. 清理（后处理 FBO → UI/音频 → 天空/渲染器 GL 资源先释放；窗口最后析构 → glfwTerminate）
    hdrFbo.destroy();
    outLdrFbo.destroy();
    SceneManager::shutdown();
    PostProcess::shutdown();
    UIRenderer::shutdown();
    AudioEngine::shutdown();
    IBL::shutdown();             // V0.5-4 波 2 合波：卷积缓存纹理/BRDF LUT 释放
    SSAO::shutdown();            // V0.5-5 波 2 合波：AO 纹理链释放
    ParticleRenderer::shutdown();  // V0.5-6 波 2 合波：粒子 VAO/VBO/贴图缓存释放
    SkyboxRenderer::shutdown();
    AsyncLoader::shutdown();     // V1.0-2 波 2 合波接线（§27.1：须先于 Renderer::shutdown——join 加载线程）
    Renderer::shutdown();
    Logger::info("[NovaPlayer] 退出");
    Logger::shutdown();   // 关闭日志文件（逐行已落盘）
    return 0;
}
