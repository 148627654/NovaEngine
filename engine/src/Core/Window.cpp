#include <NovaEngine/Core/Window.h>
#include <NovaEngine/Core/Logger.h>

#include <GLFW/glfw3.h>   // NovaEngine target 已定义 GLFW_INCLUDE_NONE，不会拉入系统 gl.h

#include <unordered_map>

namespace NovaEngine {

namespace {

// ===== GLFW 进程级初始化 =====

bool s_glfwInitialized = false;

void glfwErrorCallback(int code, const char* description) {
    Logger::error("[GLFW] 错误 {}: {}", code, description);
}

void ensureGlfwInit() {
    if (s_glfwInitialized) return;
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        Logger::error("[Window] glfwInit 失败");
        return;
    }
    s_glfwInitialized = true;
}

// ===== C 回调 → Window 实例的蹦床分发 =====
// 冻结头不允许加 friend / 公共分发方法，蹦床经 user pointer 找回窗口后，
// 用文件内回调表把事件交给对应 Window 的回调

struct CallbackHolder {
    Window::DropCallback drop;
    std::function<void(int, int)> resize;
};

std::unordered_map<GLFWwindow*, CallbackHolder> s_callbacks;

std::filesystem::path utf8Path(const char* utf8) {
    // GLFW 拖放路径为 UTF-8；Windows 下直接构造 path 会按 ANSI 代码页解释，
    // 必须经 u8string 转换（中文路径资产安全，ARCHITECTURE.md §11-3）
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8)));
}

void dropTrampoline(GLFWwindow* win, int count, const char** paths) {
    auto it = s_callbacks.find(win);
    if (it == s_callbacks.end() || !it->second.drop || !paths) return;
    std::vector<std::filesystem::path> files;
    files.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        files.push_back(utf8Path(paths[i]));
    }
    it->second.drop(files);
}

void resizeTrampoline(GLFWwindow* win, int width, int height) {
    auto it = s_callbacks.find(win);
    if (it != s_callbacks.end() && it->second.resize) {
        it->second.resize(width, height);
    }
}

}  // namespace

std::unique_ptr<Window> Window::create(const std::string& title, int width, int height) {
    ensureGlfwInit();
    if (!s_glfwInitialized) return nullptr;

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // HiDPI（2026-11-16）：窗口尺寸按「逻辑像素」创建，帧缓冲随显示器缩放
    //（150% 显示器上 1280x720 逻辑 = 1920x1080 帧缓冲）——文字/UI 以物理分辨率
    // 渲染，消除高分屏下发虚模糊；100% 显示器上此提示无效果
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!handle) {
        Logger::error("[Window] 创建窗口失败（{}x{}，OpenGL 4.5 core）", width, height);
        return nullptr;
    }

    std::unique_ptr<Window> win(new Window());
    win->m_handle = handle;
    s_callbacks.emplace(handle, CallbackHolder{});

    glfwSetWindowUserPointer(handle, win.get());
    glfwSetDropCallback(handle, dropTrampoline);
    glfwSetFramebufferSizeCallback(handle, resizeTrampoline);

    // 引擎全局只有一个窗口，创建后立即设为当前上下文（Renderer::init 的 glad 加载依赖）
    glfwMakeContextCurrent(handle);

    Logger::info("[Window] 窗口已创建：{}（{}x{}）", title, width, height);
    return win;
}

Window::~Window() {
    if (m_handle) {
        s_callbacks.erase(m_handle);
        glfwDestroyWindow(m_handle);
        m_handle = nullptr;
    }
    // 引擎全局单窗口：销毁即进程结束路径，补 glfwTerminate 释放 GLFW 进程级资源
    if (s_glfwInitialized) {
        glfwTerminate();
        s_glfwInitialized = false;
    }
}

bool Window::shouldClose() const {
    return m_handle && glfwWindowShouldClose(m_handle);
}

void Window::pollEvents() {
    // 拖放/尺寸变化等回调由 GLFW 在 glfwPollEvents 内部直接分发（经上面的蹦床）
    glfwPollEvents();
}

void Window::swapBuffers() {
    if (m_handle) glfwSwapBuffers(m_handle);
}

void Window::setVsync(bool enabled) {
    if (m_handle) glfwSwapInterval(enabled ? 1 : 0);
}

void Window::getFramebufferSize(int& outWidth, int& outHeight) const {
    if (m_handle) {
        glfwGetFramebufferSize(m_handle, &outWidth, &outHeight);
    } else {
        outWidth = 0;
        outHeight = 0;
    }
}

GLFWwindow* Window::nativeHandle() const {
    return m_handle;
}

void Window::setDropCallback(DropCallback cb) {
    m_dropCb = std::move(cb);
    if (m_handle) s_callbacks[m_handle].drop = m_dropCb;
}

void Window::setResizeCallback(std::function<void(int, int)> cb) {
    m_resizeCb = std::move(cb);
    if (m_handle) s_callbacks[m_handle].resize = m_resizeCb;
}

}  // namespace NovaEngine
