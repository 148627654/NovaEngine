#pragma once
#include <NovaEngine/Core/Input.h>

#include <filesystem>
#include <string>
#include <vector>

namespace NovaEngine {

// 动作绑定条目：键鼠/手柄多源并存（任一按下即触发）；JSON 可序列化（.novainput）
struct InputBinding {
    Key key = Key::Unknown;                    // 键盘源（Unknown = 不参与）
    MouseButton mouseButton = MouseButton::Left;
    bool useMouseButton = false;               // true = 鼠标源参与
    GamepadButton gamepadButton = GamepadButton::A;
    bool useGamepadButton = false;             // true = 手柄源参与
    int gamepadIndex = 0;                      // 手柄序号（0..Input::kMaxGamepads-1）
    bool ctrl = false, shift = false, alt = false;   // 修饰键：按住才生效；手柄源忽略修饰
};

// 轴绑定条目：键盘正负键 + 可选手柄轴
struct AxisBinding {
    Key negativeKey = Key::Unknown;            // Unknown = 无
    Key positiveKey = Key::Unknown;
    GamepadAxis gamepadAxis = GamepadAxis::LeftX;
    bool useGamepadAxis = false;
    float gamepadScale = 1.0f;                 // 乘数（负 = 反向）
    bool ctrl = false, shift = false, alt = false;
};

// 输入绑定表（全静态）：动作/轴名 → 绑定列表；.novainput JSON 读写（契约 §24.1）
class InputMap {
public:
    static void beginFrame();  // 每帧一次，紧跟 Input::beginFrame 之后（editor/player 主循环接线，见 §24.5）

    // 配置（.novainput JSON；失败 = Logger::warn + 保留旧表；首启无文件 = 内置默认绑定）
    static bool load(const std::filesystem::path& path);
    static bool save(const std::filesystem::path& path);
    static void clear();       // 清空全部动作/轴（换项目时由编辑器调用）

    // 脚本查询（Play 中 onUpdate 内调用）
    static bool hasAction(const std::string& name);
    static bool getAction(const std::string& name);        // 按住（任意源）
    static bool getActionDown(const std::string& name);    // 本帧按下边沿
    static bool getActionUp(const std::string& name);      // 本帧松开边沿
    static float getAxis(const std::string& name);         // -1..1；缺失 warn 一次返回 0

    // 绑定管理（编辑器 InputBindingsPanel 用；改动即时生效）
    static const std::vector<std::string>& actionNames();
    static const std::vector<std::string>& axisNames();
    static void addAction(const std::string& name);        // 重名 warn + 忽略
    static void removeAction(const std::string& name);
    static void addAxis(const std::string& name);          // 重名 warn + 忽略
    static void removeAxis(const std::string& name);
    static void setActionBindings(const std::string& name, const std::vector<InputBinding>& b);
    static void setAxisBindings(const std::string& name, const std::vector<AxisBinding>& b);
    static const std::vector<InputBinding>& actionBindings(const std::string& name);  // 未知名返回空表
    static const std::vector<AxisBinding>& axisBindings(const std::string& name);

    // 键名 ↔ 枚举（绑定面板下拉与 JSON 读写共用）
    static std::string keyName(Key k);
    static Key keyFromName(const std::string& name);       // 未知返回 Key::Unknown
    static std::string gamepadButtonName(GamepadButton b);
    static GamepadButton gamepadButtonFromName(const std::string& name);   // 未知返回 GamepadButton::Count
    static std::string gamepadAxisName(GamepadAxis a);
    static GamepadAxis gamepadAxisFromName(const std::string& name);       // 未知返回 GamepadAxis::Count
};

}  // namespace NovaEngine
