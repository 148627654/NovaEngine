#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;  // 全局前向声明（而非 NovaEngine 命名空间内），保证与 ImGui_ImplGlfw 的类型一致；公共头不泄漏 GLFW

namespace NovaEngine {

class Window {
public:
    static std::unique_ptr<Window> create(const std::string& title, int width, int height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void pollEvents();                              // 内部 glfwPollEvents + 分发回调
    void swapBuffers();
    void setVsync(bool enabled);
    void getFramebufferSize(int& outWidth, int& outHeight) const;
    // 内容缩放（V1.0-12 §28.2.1 追加）：窗口逻辑像素 → 帧缓冲像素比
    //（glfwGetWindowContentScale 封装；GLFW_SCALE_TO_MONITOR 下 = 显示器缩放比，
    // 150% = 1.5）。供「窗口逻辑 → 布局空间」统一换算（UIRenderer::layoutFromWindowPoint）。
    void getContentScale(float& outX, float& outY) const;
    GLFWwindow* nativeHandle() const;

    using DropCallback = std::function<void(const std::vector<std::filesystem::path>&)>;
    void setDropCallback(DropCallback cb);          // 外部文件拖入窗口（Explorer 拖拽）
    void setResizeCallback(std::function<void(int, int)> cb);

private:
    Window() = default;
    GLFWwindow* m_handle = nullptr;
    DropCallback m_dropCb;
    std::function<void(int, int)> m_resizeCb;
    // GLFW C 回调通过 glfwSetWindowUserPointer(this) + static 蹦床分发
};

}  // namespace NovaEngine
