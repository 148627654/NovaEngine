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

#include <windows.h>   // SetConsoleOutputCP + GetModuleFileNameW（须在 glad 之前）

#include <NovaEngine/NovaEngine.h>

#include <glad/gl.h>                    // glViewport
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

    // 1. 窗口（先于 Renderer::init：GL 上下文）
    auto window = Window::create("NovaPlayer", args.width, args.height);
    if (!window) {
        Logger::error("[NovaPlayer] 窗口创建失败（GLFW 初始化错误）");
        return 1;
    }
    window->setVsync(true);

    // 2. 项目根 = exe 目录（AssetDatabase 相对路径锚点；AssetStream pak 降维锚点）
    AssetDatabase::setProjectRoot(exeDir);

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

    // 5. 场景加载（AssetStream：先查 pak 再查磁盘）+ Play 启动
    Scene scene("Player");
    if (!SceneSerializer::loadFromFile(scene, args.scene)) {
        Logger::error("[NovaPlayer] 场景加载失败: {}", args.scene.string());
        return 1;
    }
    scene.start();

    // 6. 主循环（无相机时 fallback 相机兜底；场景相机经 collectCamera 优先）
    RenderCamera fallback;
    fallback.position = glm::vec3(0.0f, 1.5f, 8.0f);
    fallback.view = glm::lookAt(fallback.position, glm::vec3(0.0f, 1.0f, 0.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f));
    fallback.projection = glm::mat4(1.0f);

    while (!window->shouldClose()) {
        window->pollEvents();
        Time::beginFrame();
        Input::beginFrame();

        if (Input::isKeyPressed(Key::Escape)) break;

        // 视口跟随帧缓冲（分辨率自适应）；fallback 投影供场景相机 aspect 推导
        int fbW = args.width, fbH = args.height;
        window->getFramebufferSize(fbW, fbH);
        const float aspect = fbH > 0 ? static_cast<float>(fbW) / static_cast<float>(fbH)
                                     : 16.0f / 9.0f;
        fallback.projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        glViewport(0, 0, fbW, fbH);

        scene.update(Time::deltaTime());
        SceneRenderer::renderScene(scene, fallback);
        window->swapBuffers();
    }

    // 7. 清理（GL 资源先释放；窗口最后析构 → glfwTerminate）
    SkyboxRenderer::shutdown();
    Renderer::shutdown();
    Logger::info("[NovaPlayer] 退出");
    return 0;
}
