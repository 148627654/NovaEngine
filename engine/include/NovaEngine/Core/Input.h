#pragma once
#include <cstdint>
#include <glm/vec2.hpp>

namespace NovaEngine {

// 键码值域与 GLFW 一致（Input.cpp 内 static_assert 校验），编辑器常用键全覆盖
enum class Key : int32_t {
    Unknown = -1,
    Space = 32,
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
    Right = 262, Left = 263, Down = 264, Up = 265,
    PageUp = 266, PageDown = 267, Home = 268, End = 269,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift = 340, LeftControl = 341, LeftAlt = 342,
    RightShift = 344, RightControl = 345, RightAlt = 346
};

enum class MouseButton : int32_t { Left = 0, Right = 1, Middle = 2 };

// 手柄按钮，值域与 GLFW_GAMEPAD_BUTTON_* 一致（Input.cpp 内 static_assert 校验，与 Key/MouseButton 同纪律）
enum class GamepadButton : int32_t {
    A = 0, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide,
    LeftThumb, RightThumb, DPadUp, DPadRight, DPadDown, DPadLeft, Count
};

// 手柄轴，值域与 GLFW_GAMEPAD_AXIS_* 一致
enum class GamepadAxis : int32_t {
    LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count
};

class Input {
public:
    static void beginFrame();                       // 每帧一次：快照边沿状态（须在 pollEvents 之后调用）
    static bool isKeyDown(Key key);                 // 按住
    static bool isKeyPressed(Key key);              // 本帧刚按下
    static bool isKeyReleased(Key key);             // 本帧刚松开
    static bool isMouseDown(MouseButton button);
    static bool isMousePressed(MouseButton button);
    static bool isMouseReleased(MouseButton button);
    static glm::vec2 mousePosition();               // 窗口逻辑坐标（左上原点）
    static glm::vec2 mouseDelta();
    static glm::vec2 scrollDelta();

    // ===== 手柄（V0.5-11 追加；beginFrame 内自动轮询 GLFW_JOYSTICK_1..4）=====
    static constexpr int kMaxGamepads = 4;          // 最多同时轮询的手柄数
    static bool isGamepadConnected(int j = 0);      // j = 手柄序号（0..kMaxGamepads-1）
    static int connectedGamepadCount();
    static const char* gamepadName(int j = 0);      // 未连接返回 nullptr；返回指针仅当帧有效
    static bool isGamepadButtonDown(GamepadButton b, int j = 0);
    static bool isGamepadButtonPressed(GamepadButton b, int j = 0);   // 本帧刚按下（边沿）
    static bool isGamepadButtonReleased(GamepadButton b, int j = 0);  // 本帧刚松开（边沿）
    static float gamepadAxis(GamepadAxis a, int j = 0);   // -1..1，死区已应用
    static void setGamepadDeadzone(float d);              // 默认 0.15，钳制 [0,1)
};

}  // namespace NovaEngine
