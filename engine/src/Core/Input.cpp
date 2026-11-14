#include <NovaEngine/Core/Input.h>

#include <GLFW/glfw3.h>

#include <cstring>

namespace NovaEngine {

// ===== 编译期校验：Key / MouseButton 枚举值域与 GLFW 键码一致（改枚举即编译失败）=====
static_assert(static_cast<int>(Key::Space) == GLFW_KEY_SPACE);
static_assert(static_cast<int>(Key::Num0) == GLFW_KEY_0);
static_assert(static_cast<int>(Key::Num1) == GLFW_KEY_1);
static_assert(static_cast<int>(Key::Num2) == GLFW_KEY_2);
static_assert(static_cast<int>(Key::Num3) == GLFW_KEY_3);
static_assert(static_cast<int>(Key::Num4) == GLFW_KEY_4);
static_assert(static_cast<int>(Key::Num5) == GLFW_KEY_5);
static_assert(static_cast<int>(Key::Num6) == GLFW_KEY_6);
static_assert(static_cast<int>(Key::Num7) == GLFW_KEY_7);
static_assert(static_cast<int>(Key::Num8) == GLFW_KEY_8);
static_assert(static_cast<int>(Key::Num9) == GLFW_KEY_9);
static_assert(static_cast<int>(Key::A) == GLFW_KEY_A);
static_assert(static_cast<int>(Key::B) == GLFW_KEY_B);
static_assert(static_cast<int>(Key::C) == GLFW_KEY_C);
static_assert(static_cast<int>(Key::D) == GLFW_KEY_D);
static_assert(static_cast<int>(Key::E) == GLFW_KEY_E);
static_assert(static_cast<int>(Key::F) == GLFW_KEY_F);
static_assert(static_cast<int>(Key::G) == GLFW_KEY_G);
static_assert(static_cast<int>(Key::H) == GLFW_KEY_H);
static_assert(static_cast<int>(Key::I) == GLFW_KEY_I);
static_assert(static_cast<int>(Key::J) == GLFW_KEY_J);
static_assert(static_cast<int>(Key::K) == GLFW_KEY_K);
static_assert(static_cast<int>(Key::L) == GLFW_KEY_L);
static_assert(static_cast<int>(Key::M) == GLFW_KEY_M);
static_assert(static_cast<int>(Key::N) == GLFW_KEY_N);
static_assert(static_cast<int>(Key::O) == GLFW_KEY_O);
static_assert(static_cast<int>(Key::P) == GLFW_KEY_P);
static_assert(static_cast<int>(Key::Q) == GLFW_KEY_Q);
static_assert(static_cast<int>(Key::R) == GLFW_KEY_R);
static_assert(static_cast<int>(Key::S) == GLFW_KEY_S);
static_assert(static_cast<int>(Key::T) == GLFW_KEY_T);
static_assert(static_cast<int>(Key::U) == GLFW_KEY_U);
static_assert(static_cast<int>(Key::V) == GLFW_KEY_V);
static_assert(static_cast<int>(Key::W) == GLFW_KEY_W);
static_assert(static_cast<int>(Key::X) == GLFW_KEY_X);
static_assert(static_cast<int>(Key::Y) == GLFW_KEY_Y);
static_assert(static_cast<int>(Key::Z) == GLFW_KEY_Z);
static_assert(static_cast<int>(Key::Escape) == GLFW_KEY_ESCAPE);
static_assert(static_cast<int>(Key::Enter) == GLFW_KEY_ENTER);
static_assert(static_cast<int>(Key::Tab) == GLFW_KEY_TAB);
static_assert(static_cast<int>(Key::Backspace) == GLFW_KEY_BACKSPACE);
static_assert(static_cast<int>(Key::Insert) == GLFW_KEY_INSERT);
static_assert(static_cast<int>(Key::Delete) == GLFW_KEY_DELETE);
static_assert(static_cast<int>(Key::Right) == GLFW_KEY_RIGHT);
static_assert(static_cast<int>(Key::Left) == GLFW_KEY_LEFT);
static_assert(static_cast<int>(Key::Down) == GLFW_KEY_DOWN);
static_assert(static_cast<int>(Key::Up) == GLFW_KEY_UP);
static_assert(static_cast<int>(Key::PageUp) == GLFW_KEY_PAGE_UP);
static_assert(static_cast<int>(Key::PageDown) == GLFW_KEY_PAGE_DOWN);
static_assert(static_cast<int>(Key::Home) == GLFW_KEY_HOME);
static_assert(static_cast<int>(Key::End) == GLFW_KEY_END);
static_assert(static_cast<int>(Key::F1) == GLFW_KEY_F1);
static_assert(static_cast<int>(Key::F2) == GLFW_KEY_F2);
static_assert(static_cast<int>(Key::F3) == GLFW_KEY_F3);
static_assert(static_cast<int>(Key::F4) == GLFW_KEY_F4);
static_assert(static_cast<int>(Key::F5) == GLFW_KEY_F5);
static_assert(static_cast<int>(Key::F6) == GLFW_KEY_F6);
static_assert(static_cast<int>(Key::F7) == GLFW_KEY_F7);
static_assert(static_cast<int>(Key::F8) == GLFW_KEY_F8);
static_assert(static_cast<int>(Key::F9) == GLFW_KEY_F9);
static_assert(static_cast<int>(Key::F10) == GLFW_KEY_F10);
static_assert(static_cast<int>(Key::F11) == GLFW_KEY_F11);
static_assert(static_cast<int>(Key::F12) == GLFW_KEY_F12);
static_assert(static_cast<int>(Key::LeftShift) == GLFW_KEY_LEFT_SHIFT);
static_assert(static_cast<int>(Key::LeftControl) == GLFW_KEY_LEFT_CONTROL);
static_assert(static_cast<int>(Key::LeftAlt) == GLFW_KEY_LEFT_ALT);
static_assert(static_cast<int>(Key::RightShift) == GLFW_KEY_RIGHT_SHIFT);
static_assert(static_cast<int>(Key::RightControl) == GLFW_KEY_RIGHT_CONTROL);
static_assert(static_cast<int>(Key::RightAlt) == GLFW_KEY_RIGHT_ALT);
static_assert(static_cast<int>(MouseButton::Left) == GLFW_MOUSE_BUTTON_LEFT);
static_assert(static_cast<int>(MouseButton::Right) == GLFW_MOUSE_BUTTON_RIGHT);
static_assert(static_cast<int>(MouseButton::Middle) == GLFW_MOUSE_BUTTON_MIDDLE);

namespace {

constexpr int kMaxKeyCode = 512;   // GLFW 键码实际范围 0..347，数组留余量

// 需要每帧轮询的键（与 Key 枚举一一对应）
constexpr Key kTrackedKeys[] = {
    Key::Space,
    Key::Num0, Key::Num1, Key::Num2, Key::Num3, Key::Num4,
    Key::Num5, Key::Num6, Key::Num7, Key::Num8, Key::Num9,
    Key::A, Key::B, Key::C, Key::D, Key::E, Key::F, Key::G, Key::H, Key::I,
    Key::J, Key::K, Key::L, Key::M, Key::N, Key::O, Key::P, Key::Q, Key::R,
    Key::S, Key::T, Key::U, Key::V, Key::W, Key::X, Key::Y, Key::Z,
    Key::Escape, Key::Enter, Key::Tab, Key::Backspace, Key::Insert, Key::Delete,
    Key::Right, Key::Left, Key::Down, Key::Up,
    Key::PageUp, Key::PageDown, Key::Home, Key::End,
    Key::F1, Key::F2, Key::F3, Key::F4, Key::F5, Key::F6,
    Key::F7, Key::F8, Key::F9, Key::F10, Key::F11, Key::F12,
    Key::LeftShift, Key::LeftControl, Key::LeftAlt,
    Key::RightShift, Key::RightControl, Key::RightAlt,
};

// 当前帧 / 上一帧状态（边沿检测 = 两帧状态对比）
bool g_keyDown[kMaxKeyCode] = {};
bool g_keyPrevDown[kMaxKeyCode] = {};
bool g_mouseDown[3] = {};
bool g_mousePrevDown[3] = {};
glm::vec2 g_mousePos{0.0f};
glm::vec2 g_prevMousePos{0.0f};
glm::vec2 g_mouseDelta{0.0f};
glm::vec2 g_scrollDelta{0.0f};

// 滚轮是事件型输入（GLFW 无查询接口），用回调累积，beginFrame 时快照并清零。
// 注意：安装回调时保存并链接上一个回调（ImGui GLFW 后端只在 Init 时安装一次，
// 直接覆盖会令 ImGui 的列表滚动失效）
float g_scrollAccumX = 0.0f;
float g_scrollAccumY = 0.0f;
GLFWwindow* g_scrollRegisteredWindow = nullptr;
GLFWscrollfun g_prevScrollCallback = nullptr;

void scrollCallback(GLFWwindow* win, double xoffset, double yoffset) {
    if (g_prevScrollCallback) g_prevScrollCallback(win, xoffset, yoffset);   // 先转发给上一回调（如 ImGui）
    g_scrollAccumX += static_cast<float>(xoffset);
    g_scrollAccumY += static_cast<float>(yoffset);
}

int toIndex(Key key) {
    const int code = static_cast<int>(key);
    return (code >= 0 && code < kMaxKeyCode) ? code : -1;
}

int toIndex(MouseButton button) {
    const int code = static_cast<int>(button);
    return (code >= 0 && code < 3) ? code : -1;
}

}  // namespace

void Input::beginFrame() {
    GLFWwindow* win = glfwGetCurrentContext();

    // 懒注册滚轮回调（窗口重建后自动重新注册；保存上一回调并链式转发）
    if (win && win != g_scrollRegisteredWindow) {
        g_scrollRegisteredWindow = win;
        g_prevScrollCallback = glfwSetScrollCallback(win, scrollCallback);
    }

    // 滚动快照：本帧读取后清零累积
    g_scrollDelta = glm::vec2(g_scrollAccumX, g_scrollAccumY);
    g_scrollAccumX = 0.0f;
    g_scrollAccumY = 0.0f;

    // 上一帧状态快照
    std::memcpy(g_keyPrevDown, g_keyDown, sizeof(g_keyDown));
    std::memcpy(g_mousePrevDown, g_mouseDown, sizeof(g_mouseDown));

    if (win) {
        for (Key key : kTrackedKeys) {
            g_keyDown[static_cast<int>(key)] =
                glfwGetKey(win, static_cast<int>(key)) == GLFW_PRESS;
        }
        for (int b = 0; b < 3; ++b) {
            g_mouseDown[b] = glfwGetMouseButton(win, b) == GLFW_PRESS;
        }
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(win, &mx, &my);
        g_prevMousePos = g_mousePos;
        g_mousePos = glm::vec2(static_cast<float>(mx), static_cast<float>(my));
        g_mouseDelta = g_mousePos - g_prevMousePos;   // 首帧为 0
    }
}

bool Input::isKeyDown(Key key) {
    const int i = toIndex(key);
    return i >= 0 && g_keyDown[i];
}

bool Input::isKeyPressed(Key key) {
    const int i = toIndex(key);
    return i >= 0 && g_keyDown[i] && !g_keyPrevDown[i];
}

bool Input::isKeyReleased(Key key) {
    const int i = toIndex(key);
    return i >= 0 && !g_keyDown[i] && g_keyPrevDown[i];
}

bool Input::isMouseDown(MouseButton button) {
    const int i = toIndex(button);
    return i >= 0 && g_mouseDown[i];
}

bool Input::isMousePressed(MouseButton button) {
    const int i = toIndex(button);
    return i >= 0 && g_mouseDown[i] && !g_mousePrevDown[i];
}

bool Input::isMouseReleased(MouseButton button) {
    const int i = toIndex(button);
    return i >= 0 && !g_mouseDown[i] && g_mousePrevDown[i];
}

glm::vec2 Input::mousePosition() { return g_mousePos; }
glm::vec2 Input::mouseDelta() { return g_mouseDelta; }
glm::vec2 Input::scrollDelta() { return g_scrollDelta; }

}  // namespace NovaEngine
