// main.cpp —— NovaEditor 入口（§3.1 主循环的前置初始化步骤）
//
// 初始化顺序（严格照 §3.1）：
//   Window::create → Renderer::init → AssetDatabase::setProjectRoot
//   → ImGui 上下文 + docking + 样式 + 中文字体
//   → ImGui_ImplGlfw_InitForOpenGL / ImGui_ImplOpenGL3_Init
//   → EditorApplication.run()（内部 init/主循环/shutdown）

#include <NovaEngine/NovaEngine.h>

#include "EditorApplication.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <filesystem>
#include <memory>

int main() {
    using namespace NovaEngine;

    Logger::info("NovaEditor 启动");

    // 1. 窗口
    auto window = Window::create("NovaEditor", 1600, 900);
    if (!window) {
        Logger::error("窗口创建失败（GLFW 初始化错误）");
        return 1;
    }
    window->setVsync(true);

    // 2. 渲染器（必须在窗口上下文创建之后）
    Renderer::init();

    // 3. 项目根（当前工作目录；VS 调试运行目录已设为项目根）
    AssetDatabase::setProjectRoot(std::filesystem::current_path());
    Logger::info("项目根目录: {}", AssetDatabase::projectRoot().string());

    // 4. ImGui 上下文 + docking + 样式 + 中文字体
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // ViewportsEnable 默认关（§11）
    ImGui::StyleColorsDark();

    // 默认字体 + 合并中文字体（msyh.ttc 不存在时回退纯默认字体，§11.9）
    // ImGui 1.93 新字体图集：目标字体显式基准字号 16px，合并字体同样显式 16px 才合法
    //（隐式字号目标 + 显式字号合并会触发 imgui_draw.cpp 的 MergeMode 断言）
    ImFontConfig baseCfg{};
    baseCfg.SizePixels = 16.0f;
    io.Fonts->AddFontDefault(&baseCfg);
    ImFontConfig fontCfg{};
    fontCfg.MergeMode = true;  // 合并进默认字体图集，避免双字体切换
    if (!io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 16.0f, &fontCfg,
                                      io.Fonts->GetGlyphRangesChineseFull()))
        Logger::warn("中文字体加载失败（C:/Windows/Fonts/msyh.ttc），回退默认字体");

    // 5. ImGui 后端（GLFW 回调会链式调用 Window 已安装的回调，Input 不受影响）
    if (!ImGui_ImplGlfw_InitForOpenGL(window->nativeHandle(), true)) {
        Logger::error("ImGui GLFW 后端初始化失败");
        return 1;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 450")) {
        Logger::error("ImGui OpenGL3 后端初始化失败");
        return 1;
    }

    // 6. 应用主循环；run() 结束时内部调用 shutdown() 清理 ImGui 与 Renderer，
    //    窗口由 EditorApplication 析构最后释放（GL 上下文随窗口销毁）。
    NovaEditor::EditorApplication app(std::move(window));
    return app.run();
}
