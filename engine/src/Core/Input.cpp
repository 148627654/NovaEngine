#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/Logger.h>

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstring>
#include <string>

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

// ===== 编译期校验：GamepadButton/GamepadAxis 枚举值域与 GLFW_GAMEPAD_* 一致（改枚举即编译失败）=====
static_assert(static_cast<int>(GamepadButton::A) == GLFW_GAMEPAD_BUTTON_A);
static_assert(static_cast<int>(GamepadButton::B) == GLFW_GAMEPAD_BUTTON_B);
static_assert(static_cast<int>(GamepadButton::X) == GLFW_GAMEPAD_BUTTON_X);
static_assert(static_cast<int>(GamepadButton::Y) == GLFW_GAMEPAD_BUTTON_Y);
static_assert(static_cast<int>(GamepadButton::LeftBumper) == GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
static_assert(static_cast<int>(GamepadButton::RightBumper) == GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
static_assert(static_cast<int>(GamepadButton::Back) == GLFW_GAMEPAD_BUTTON_BACK);
static_assert(static_cast<int>(GamepadButton::Start) == GLFW_GAMEPAD_BUTTON_START);
static_assert(static_cast<int>(GamepadButton::Guide) == GLFW_GAMEPAD_BUTTON_GUIDE);
static_assert(static_cast<int>(GamepadButton::LeftThumb) == GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
static_assert(static_cast<int>(GamepadButton::RightThumb) == GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
static_assert(static_cast<int>(GamepadButton::DPadUp) == GLFW_GAMEPAD_BUTTON_DPAD_UP);
static_assert(static_cast<int>(GamepadButton::DPadRight) == GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
static_assert(static_cast<int>(GamepadButton::DPadDown) == GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
static_assert(static_cast<int>(GamepadButton::DPadLeft) == GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
static_assert(static_cast<int>(GamepadButton::Count) == GLFW_GAMEPAD_BUTTON_LAST + 1);
static_assert(static_cast<int>(GamepadAxis::LeftX) == GLFW_GAMEPAD_AXIS_LEFT_X);
static_assert(static_cast<int>(GamepadAxis::LeftY) == GLFW_GAMEPAD_AXIS_LEFT_Y);
static_assert(static_cast<int>(GamepadAxis::RightX) == GLFW_GAMEPAD_AXIS_RIGHT_X);
static_assert(static_cast<int>(GamepadAxis::RightY) == GLFW_GAMEPAD_AXIS_RIGHT_Y);
static_assert(static_cast<int>(GamepadAxis::LeftTrigger) == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER);
static_assert(static_cast<int>(GamepadAxis::RightTrigger) == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER);
static_assert(static_cast<int>(GamepadAxis::Count) == GLFW_GAMEPAD_AXIS_LAST + 1);

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

// ===== 手柄状态（每帧轮询快照，与键鼠同一「当前/上一帧」边沿纪律）=====
constexpr int kGamepadButtonCount = static_cast<int>(GamepadButton::Count);   // 15（含 DPad 方向）
constexpr int kGamepadAxisCount = static_cast<int>(GamepadAxis::Count);       // 6

bool g_gpConnected[Input::kMaxGamepads] = {};
bool g_gpMapped[Input::kMaxGamepads] = {};          // glfwJoystickIsGamepad 结果
bool g_gpUnmappedWarned[Input::kMaxGamepads] = {};  // 第三方无映射设备 warn 一次（每次连接重置）
std::string g_gpNames[Input::kMaxGamepads];
bool g_gpButtonDown[Input::kMaxGamepads][kGamepadButtonCount] = {};
bool g_gpButtonPrevDown[Input::kMaxGamepads][kGamepadButtonCount] = {};
float g_gpAxes[Input::kMaxGamepads][kGamepadAxisCount] = {};
float g_gpDeadzone = 0.15f;   // 默认死区 0.15，gamepadAxis 读取时应用

int toIndex(GamepadButton b) {
    const int code = static_cast<int>(b);
    return (code >= 0 && code < kGamepadButtonCount) ? code : -1;
}

int toIndex(GamepadAxis a) {
    const int code = static_cast<int>(a);
    return (code >= 0 && code < kGamepadAxisCount) ? code : -1;
}

void clearGamepadState(int j) {
    std::memset(g_gpButtonDown[j], 0, sizeof(g_gpButtonDown[j]));
    std::memset(g_gpButtonPrevDown[j], 0, sizeof(g_gpButtonPrevDown[j]));
    std::memset(g_gpAxes[j], 0, sizeof(g_gpAxes[j]));
}

// 每帧轮询 GLFW_JOYSTICK_1..4（手柄查询不依赖窗口上下文，故在 beginFrame 的 win 判定之外调用）：
//   映射设备（glfwJoystickIsGamepad 为真）→ glfwGetGamepadState 映射为边沿快照；
//   连接/断开状态变化各打一次日志（含设备名）；断开清零全部状态防按键粘连；
//   非映射第三方设备（glfwJoystickPresent 但非 gamepad）→ 连接可见（name 有效）、输入恒 0、warn 一次。
void pollGamepads() {
    for (int j = 0; j < Input::kMaxGamepads; ++j) {
        const int jid = GLFW_JOYSTICK_1 + j;
        if (glfwJoystickPresent(jid) == 0) {
            if (g_gpConnected[j]) {
                Logger::warn("[Input] 手柄 {} 已断开: {}", j + 1, g_gpNames[j]);
                g_gpConnected[j] = false;
                g_gpMapped[j] = false;
                g_gpUnmappedWarned[j] = false;
                clearGamepadState(j);
            }
            continue;
        }

        const char* rawName = glfwGetJoystickName(jid);
        const std::string name = rawName ? rawName : "";
        if (!g_gpConnected[j]) {
            Logger::info("[Input] 手柄 {} 已连接: {}", j + 1, name);
            g_gpConnected[j] = true;
            g_gpMapped[j] = false;
            g_gpUnmappedWarned[j] = false;
            clearGamepadState(j);
        }
        g_gpNames[j] = name;

        const bool mapped = glfwJoystickIsGamepad(jid) != 0;
        if (mapped && !g_gpMapped[j]) g_gpUnmappedWarned[j] = false;   // 重新获得映射后再次丢失可重新告警
        g_gpMapped[j] = mapped;
        if (!mapped) {
            if (!g_gpUnmappedWarned[j]) {
                Logger::warn("[Input] 手柄 {} 无 GLFW 映射（第三方设备），输入恒为 0: {}", j + 1, name);
                g_gpUnmappedWarned[j] = true;
            }
            clearGamepadState(j);   // 非映射：连接可见、输入全 0
            continue;
        }

        // 映射设备：先快照上一帧按钮状态，再读当前状态（边沿 = 两帧对比）
        std::memcpy(g_gpButtonPrevDown[j], g_gpButtonDown[j], sizeof(g_gpButtonDown[j]));
        GLFWgamepadstate st;
        if (glfwGetGamepadState(jid, &st) == GLFW_TRUE) {
            for (int b = 0; b < kGamepadButtonCount; ++b)
                g_gpButtonDown[j][b] = st.buttons[b] == GLFW_PRESS;
            for (int a = 0; a < kGamepadAxisCount; ++a)
                g_gpAxes[j][a] = st.axes[a];
        } else {
            // 极端情况（轮询失败）：按全 0 处理，不粘连旧状态
            std::memset(g_gpButtonDown[j], 0, sizeof(g_gpButtonDown[j]));
            std::memset(g_gpAxes[j], 0, sizeof(g_gpAxes[j]));
        }
    }
}

}  // namespace

// ===== V1.0 按键调试日志（环境变量 NOVA_DBG_KEYS=1 开启，2026-11-23 用户需求）=====
// 按下/松开边沿逐键打日志——验收与排障时确认按键是否到达引擎
//（编辑器 Console 面板与终端/日志文件均可见）。生产环境不设该变量 = 零开销。
const char* keyDebugName(Key key) {
    if (key >= Key::A && key <= Key::Z) {
        static char letter[2] = {'A', '\0'};
        letter[0] = static_cast<char>('A' + (static_cast<int>(key) - static_cast<int>(Key::A)));
        return letter;
    }
    switch (key) {
        case Key::Space: return "Space";
        case Key::Num0: return "Num0"; case Key::Num1: return "Num1";
        case Key::Num2: return "Num2"; case Key::Num3: return "Num3";
        case Key::Num4: return "Num4"; case Key::Num5: return "Num5";
        case Key::Num6: return "Num6"; case Key::Num7: return "Num7";
        case Key::Num8: return "Num8"; case Key::Num9: return "Num9";
        case Key::Escape: return "Escape"; case Key::Enter: return "Enter";
        case Key::Tab: return "Tab"; case Key::Backspace: return "Backspace";
        case Key::Insert: return "Insert"; case Key::Delete: return "Delete";
        case Key::Right: return "Right"; case Key::Left: return "Left";
        case Key::Down: return "Down"; case Key::Up: return "Up";
        case Key::PageUp: return "PageUp"; case Key::PageDown: return "PageDown";
        case Key::Home: return "Home"; case Key::End: return "End";
        case Key::F1: return "F1"; case Key::F2: return "F2"; case Key::F3: return "F3";
        case Key::F4: return "F4"; case Key::F5: return "F5"; case Key::F6: return "F6";
        case Key::F7: return "F7"; case Key::F8: return "F8"; case Key::F9: return "F9";
        case Key::F10: return "F10"; case Key::F11: return "F11"; case Key::F12: return "F12";
        case Key::LeftShift: return "LeftShift"; case Key::LeftControl: return "LeftControl";
        case Key::LeftAlt: return "LeftAlt"; case Key::RightShift: return "RightShift";
        case Key::RightControl: return "RightControl"; case Key::RightAlt: return "RightAlt";
        default: return "Unknown";
    }
}

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
        // 按键调试日志（NOVA_DBG_KEYS=1）：按下/松开边沿（与 isKeyPressed/Released 同源判定）
        if (std::getenv("NOVA_DBG_KEYS") != nullptr) {
            for (Key key : kTrackedKeys) {
                const int i = static_cast<int>(key);
                if (g_keyDown[i] && !g_keyPrevDown[i])
                    Logger::info("[Input] 按下 {} 键", keyDebugName(key));
                else if (!g_keyDown[i] && g_keyPrevDown[i])
                    Logger::info("[Input] 松开 {} 键", keyDebugName(key));
            }
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

    // 手柄轮询放在 win 判定之外：GLFW 手柄查询不依赖窗口上下文
    pollGamepads();
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

// ===== 手柄查询 =====

bool Input::isGamepadConnected(int j) {
    return j >= 0 && j < kMaxGamepads && g_gpConnected[j];
}

int Input::connectedGamepadCount() {
    int n = 0;
    for (int j = 0; j < kMaxGamepads; ++j)
        if (g_gpConnected[j]) ++n;
    return n;
}

const char* Input::gamepadName(int j) {
    if (j < 0 || j >= kMaxGamepads || !g_gpConnected[j]) return nullptr;
    return g_gpNames[j].c_str();   // 指针仅当帧有效（下一帧 beginFrame 可能改写）
}

bool Input::isGamepadButtonDown(GamepadButton b, int j) {
    if (j < 0 || j >= kMaxGamepads) return false;
    const int i = toIndex(b);
    return i >= 0 && g_gpButtonDown[j][i];
}

bool Input::isGamepadButtonPressed(GamepadButton b, int j) {
    if (j < 0 || j >= kMaxGamepads) return false;
    const int i = toIndex(b);
    return i >= 0 && g_gpButtonDown[j][i] && !g_gpButtonPrevDown[j][i];
}

bool Input::isGamepadButtonReleased(GamepadButton b, int j) {
    if (j < 0 || j >= kMaxGamepads) return false;
    const int i = toIndex(b);
    return i >= 0 && !g_gpButtonDown[j][i] && g_gpButtonPrevDown[j][i];
}

float Input::gamepadAxis(GamepadAxis a, int j) {
    if (j < 0 || j >= kMaxGamepads) return 0.0f;
    const int i = toIndex(a);
    if (i < 0) return 0.0f;
    float v = g_gpAxes[j][i];
    if (std::abs(v) < g_gpDeadzone) v = 0.0f;   // 死区在读取时应用（|v| < deadzone → 0）
    return v;
}

void Input::setGamepadDeadzone(float d) {
    // NaN/Inf：钳制比较恒 false 会原样写入并污染全部轴读取，直接拒绝
    if (!std::isfinite(d)) return;
    // 钳制 [0,1)：死区 ≥1 时所有轴恒为 0，无意义
    if (d < 0.0f) d = 0.0f;
    if (d >= 1.0f) d = 0.99f;
    g_gpDeadzone = d;
}

}  // namespace NovaEngine
