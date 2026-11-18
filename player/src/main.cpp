// main.cpp —— NovaPlayer 入口（§10.5：打包运行时播放器，最小主循环，无 ImGui）
//
// 初始化顺序：SetConsoleOutputCP(UTF8) → 解析参数 → Window::create → vsync
//   → Renderer::init + SkyboxRenderer::init → AssetDatabase::setProjectRoot(exe 目录)
//   → AssetStream::mountPak（默认 exe 同目录第一个 *.novapak，可 --pak 显式指定）
//   → Scene + SceneSerializer::loadFromFile → scene.start()
//   → 主循环（pollEvents/Time/Input → scene.update(dt) → SceneRenderer::renderScene
//     → swapBuffers；Esc 退出）
//   → shutdown 清理（SkyboxRenderer → Renderer → 窗口析构 glfwTerminate）
//
// 参数：--scene（默认 assets/scenes/demo.nova.json，项目内相对路径）
//       --pak（默认自动扫描 exe 同目录 *.novapak）--width/--height（默认 1280x720）
//
// 网络帧同步 spike（V0.5-15，分支 spike/network-v1，不进 main）：
//   --net host|join 进入锁步模式（跳过场景文件加载，程序化构建 NetSpike 样例场景）；
//   --net-ip <ip>（join 用，默认 127.0.0.1）--net-port（默认 27777）
//   --net-delay <帧>（输入延迟缓冲，默认 3）--net-lag <ms>（延迟注入，验收用，默认 0）
//   --net-automove（自动输入脚本，单机双进程验证免按键）--net-duration <秒>（到时自动退出）

#include <windows.h>   // SetConsoleOutputCP + GetModuleFileNameW（须在 glad 之前）

#include <NovaEngine/NovaEngine.h>
#include <NovaEngine/Network/NetSpikeSample.h>
#include <NovaEngine/Network/NetworkManager.h>

#include <glad/gl.h>                    // glViewport
#include <GLFW/glfw3.h>                  // glfwGetWindowSize（窗口逻辑尺寸；GLFW_INCLUDE_NONE 已定义）
#include <glm/gtc/matrix_transform.hpp> // lookAt / perspective / radians

#include <cstring>
#include <filesystem>
#include <string>

namespace {

struct PlayerArgs {
    std::filesystem::path scene = "assets/scenes/demo.nova.json";
    std::filesystem::path pak;   // 空 = 自动扫描 exe 目录
    int width = 1280;
    int height = 720;
    // ===== 网络帧同步 spike（V0.5-15，分支 spike/network-v1）=====
    std::string netMode;                  // "" | "host" | "join"
    std::string netIp = "127.0.0.1";      // join 目标（默认本机回环，单机双进程验证）
    int netPort = 27777;
    int netDelay = 3;                     // 输入延迟缓冲帧数
    int netLag = 0;                       // 延迟注入 ms（验收用）
    bool netAutoMove = false;             // 自动输入脚本（单机双进程验证）
    int netDuration = 0;                  // 秒数，>0 时到时自动退出（验证用）
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

// 非负整数（--net-delay/--net-lag 允许 0）
bool parseIntNonNeg(const char* s, int& out) {
    try {
        size_t consumed = 0;
        const long v = std::stol(s, &consumed);
        if (consumed != std::strlen(s) || v < 0 || v > 16384) return false;
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
        } else if (arg == "--net" && hasValue) {
            args.netMode = argv[++i];
            if (args.netMode != "host" && args.netMode != "join") {
                NovaEngine::Logger::warn("[NovaPlayer] --net 仅支持 host|join，收到: {}",
                                         args.netMode);
                args.netMode.clear();
            }
        } else if (arg == "--net-ip" && hasValue) {
            args.netIp = argv[++i];
        } else if (arg == "--net-port" && hasValue) {
            parseInt(argv[++i], args.netPort);
        } else if (arg == "--net-delay" && hasValue) {
            parseIntNonNeg(argv[++i], args.netDelay);
        } else if (arg == "--net-lag" && hasValue) {
            parseIntNonNeg(argv[++i], args.netLag);
        } else if (arg == "--net-automove") {
            args.netAutoMove = true;
        } else if (arg == "--net-duration" && hasValue) {
            parseIntNonNeg(argv[++i], args.netDuration);
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

// 默认 pak：exe 同目录第一个 *.novapak（§10.5）
std::filesystem::path findPakNextToExe(const std::filesystem::path& exeDir) {
    std::error_code ec;
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

    // 网络帧同步 spike（V0.5-15）：--net host|join 进入锁步模式
    const bool netEnabled = !args.netMode.empty();
    const bool netIsHost = netEnabled && args.netMode == "host";

    // 1. 窗口（先于 Renderer::init：GL 上下文）
    std::string windowTitle = "NovaPlayer";
    if (netEnabled)
        windowTitle += netIsHost ? " - NetSpike Host" : " - NetSpike Join";
    auto window = Window::create(windowTitle, args.width, args.height);
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

    // 5. 音频 + 运行时 UI + 后处理（V0.3-2/3/14）：音频失败只 warn 继续（优雅降级）；
    //   UI 失败内部 no-op；后处理失败内部降级为直通
    if (!AudioEngine::init())
        Logger::warn("[NovaPlayer] 音频设备初始化失败，继续无音频运行");
    UIRenderer::init();
    PostProcess::init();

    // 6. 场景管理（V0.3-12 波 3）：读 build 列表（缺失/损坏 → 空列表 + warn，不崩溃）
    SceneManager::init();

    // 7. 场景加载（AssetStream：先查 pak 再查磁盘）+ Play 启动
    //    网络模式：跳过文件加载，程序化构建锁步样例场景（主机/从机同一份，见 NetSpikeSample）
    Scene scene(netEnabled ? "NetSpike" : "Player");
    if (netEnabled) {
        buildNetSpikeScene(scene);
    } else if (!SceneSerializer::loadFromFile(scene, args.scene)) {
        Logger::error("[NovaPlayer] 场景加载失败: {}", args.scene.string());
        return 1;
    }
    // 输入绑定（V0.5-11）：场景加载后经 AssetStream 读 pak 内 assets/input/default.novainput；
    // 失败仅 warn（内置默认绑定兜底），不阻断启动
    InputMap::load("assets/input/default.novainput");

    // 7.5 网络启动（spike）：先于 scene.start()（NetCube::onStart 注册进锁步模拟）；
    //     日志目录 = exe/Logs（同步校验日志 NetSync_<角色>_<pid>.log）
    NetworkManager& net = NetworkManager::instance();
    net.setLogDir(exeDir / "Logs");
    if (netEnabled) {
        const bool started = netIsHost
                                 ? net.startHost(static_cast<uint16_t>(args.netPort), args.netDelay)
                                 : net.startJoin(args.netIp, static_cast<uint16_t>(args.netPort), args.netDelay);
        if (!started) return 1;   // 错误已在 startHost/startJoin 内日志
        if (args.netLag > 0) net.setSimulatedLagMs(args.netLag);
        if (args.netAutoMove) net.setAutoMove(true);
    }

    scene.start();
    if (!netEnabled) {
        // 注册为运行期场景并设为活动场景（§14.9；LoadScene 延迟切换经 flushPendingLoad 作用于本 scene）
        SceneManager::registerScene(scene.name(), args.scene);
        SceneManager::setActiveScene(&scene);
    }

    // 7. 主循环（无相机时 fallback 相机兜底；场景相机经 collectCamera 优先）
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

    while (!window->shouldClose()) {
        window->pollEvents();
        Time::beginFrame();
        Input::beginFrame();
        InputMap::beginFrame();   // 每帧一次，紧跟 Input::beginFrame 之后（契约 §24.5）

        if (Input::isKeyPressed(Key::Escape)) break;

        // 网络帧同步入口（spike）：泵网络 + 固定步进推进（内部 60Hz 累加器，锁步）
        // 放在 Input::beginFrame 之后（读到本帧输入）、scene.update 之前（先推进模拟再广播脚本）
        if (netEnabled) net.update(Time::deltaTime());

        // 验证模式：--net-duration 到时自动退出（同步日志逐行已落盘）
        if (netEnabled && args.netDuration > 0 &&
            Time::totalTime() >= static_cast<float>(args.netDuration))
            break;

        // 视口跟随帧缓冲（分辨率自适应）；fallback 投影供场景相机 aspect 推导
        int fbW = args.width, fbH = args.height;
        window->getFramebufferSize(fbW, fbH);
        const float aspect = fbH > 0 ? static_cast<float>(fbW) / static_cast<float>(fbH)
                                     : 16.0f / 9.0f;
        fallback.projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        glViewport(0, 0, fbW, fbH);

        // UI 输入（V0.3-3 契约：Scene::update 之前调用 → 脚本 onUpdate 读到本帧按钮状态）。
        // HiDPI：Input 鼠标为窗口逻辑坐标，UI 渲染按帧缓冲尺寸——按缩放比换算（评审 P2-5）
        int winW = args.width, winH = args.height;
        glfwGetWindowSize(window->nativeHandle(), &winW, &winH);
        // HiDPI（2026-11-16）：UI 全局缩放 = 显示器内容缩放（150% = 1.5）——GLFW
        // SCALE_TO_MONITOR 下窗口/帧缓冲同倍放大，比值恒 1，须用内容缩放才能
        // 把 fontSize 等逻辑尺寸映射到物理像素（文字按 1.5 倍物理分辨率渲染不发虚）
        float csx = 1.0f, csy = 1.0f;
        glfwGetWindowContentScale(window->nativeHandle(), &csx, &csy);
        UIRenderer::setDpiScale(std::max(csx, 1.0f));
        glm::vec2 uiMouse = Input::mousePosition();
        if (winW > 0 && winH > 0)
            uiMouse = glm::vec2(uiMouse.x * static_cast<float>(fbW) / static_cast<float>(winW),
                                uiMouse.y * static_cast<float>(fbH) / static_cast<float>(winH));
        UIRenderer::processInput(scene, uiMouse, Input::isMouseDown(MouseButton::Left),
                                 Input::isMousePressed(MouseButton::Left));

        // 帧序（§15）：scene.update（物理→脚本→协程→事件）→ SceneManager 延迟切场景
        // → 音频监听 → 3D+后处理+UI → 显示
        scene.update(Time::deltaTime());
        SceneManager::flushPendingLoad();   // §15：Scene::update 之后调用（延迟执行防广播中迭代器失效）

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
        const bool hdr = scene.postProcess.enabled && PostProcess::hdrSupported();
        if (!fbosCreated || hdr != hdrFboIsHdr) {
            hdrFbo.destroy();
            outLdrFbo.destroy();
            hdrFbo.create(fbW, fbH, 4, hdr);
            outLdrFbo.create(fbW, fbH, 0, false);
            hdrFboIsHdr = hdr;
            fbosCreated = true;
        } else {
            hdrFbo.resize(fbW, fbH);
            outLdrFbo.resize(fbW, fbH);
        }
        if (fbosCreated && hdrFbo.width() > 0 && outLdrFbo.width() > 0) {
            SceneRenderer::renderSceneToOutput(scene, fallback, hdrFbo, outLdrFbo,
                                               scene.postProcess);
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
        window->swapBuffers();
    }

    // 8. 清理（网络 → 后处理 FBO → UI/音频 → 天空/渲染器 GL 资源先释放；窗口最后析构 → glfwTerminate）
    if (netEnabled) net.shutdown();   // 关 socket + 收同步日志（在 scene 析构前：NetCube 注销安全）
    hdrFbo.destroy();
    outLdrFbo.destroy();
    SceneManager::shutdown();
    PostProcess::shutdown();
    UIRenderer::shutdown();
    AudioEngine::shutdown();
    SkyboxRenderer::shutdown();
    Renderer::shutdown();
    Logger::info("[NovaPlayer] 退出");
    Logger::shutdown();   // 关闭日志文件（逐行已落盘）
    return 0;
}
