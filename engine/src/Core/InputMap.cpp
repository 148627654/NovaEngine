// InputMap.cpp —— 输入绑定表：动作/轴名 → 键鼠/手柄绑定，.novainput JSON 读写
//（V0.2-9 补课 + V0.5-11，契约 docs/ARCHITECTURE.md §24.1）
//
// 语义（冻结裁定）：
//   动作边沿：任一绑定「本帧按下/松开 + 修饰满足」→ getActionDown/Up 为真；手柄源忽略修饰
//   轴值：全部绑定各自求值（键盘 = (正键?1:0)-(负键?1:0)；手柄 = gamepadAxis × gamepadScale，
//         死区由 Input::gamepadAxis 全局应用），修饰不满足的绑定按 0 计；
//         取 |v| 最大者（并列取绑定列表靠前者），结果恒在 -1..1
//   缺失名字：getAction* 返回 false、getAxis 返回 0，同名只 warn 一次（内部集合记录）
//   load = 整表替换（先解析到临时表，成功才提交；任何失败保留旧表）；未知键名/坏条目 warn 跳过
//   序列化（version 1）：{"version":1,"actions":{名:[{...}]},"axes":{名:[{...}]}}，
//   key/mouseButton/gamepadButton/gamepadAxis 名 = 枚举成员名字符串（mouseButton = "Left"/"Right"/"Middle"）

#include <NovaEngine/Core/InputMap.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <optional>
#include <set>
#include <utility>

namespace NovaEngine {

namespace {

// Key ↔ 枚举成员名（JSON 读写 / 绑定面板下拉共用）
constexpr std::pair<Key, const char*> kKeyNames[] = {
    {Key::Unknown, "Unknown"},
    {Key::Space, "Space"},
    {Key::Num0, "Num0"}, {Key::Num1, "Num1"}, {Key::Num2, "Num2"}, {Key::Num3, "Num3"},
    {Key::Num4, "Num4"}, {Key::Num5, "Num5"}, {Key::Num6, "Num6"}, {Key::Num7, "Num7"},
    {Key::Num8, "Num8"}, {Key::Num9, "Num9"},
    {Key::A, "A"}, {Key::B, "B"}, {Key::C, "C"}, {Key::D, "D"}, {Key::E, "E"}, {Key::F, "F"},
    {Key::G, "G"}, {Key::H, "H"}, {Key::I, "I"}, {Key::J, "J"}, {Key::K, "K"}, {Key::L, "L"},
    {Key::M, "M"}, {Key::N, "N"}, {Key::O, "O"}, {Key::P, "P"}, {Key::Q, "Q"}, {Key::R, "R"},
    {Key::S, "S"}, {Key::T, "T"}, {Key::U, "U"}, {Key::V, "V"}, {Key::W, "W"}, {Key::X, "X"},
    {Key::Y, "Y"}, {Key::Z, "Z"},
    {Key::Escape, "Escape"}, {Key::Enter, "Enter"}, {Key::Tab, "Tab"}, {Key::Backspace, "Backspace"},
    {Key::Insert, "Insert"}, {Key::Delete, "Delete"},
    {Key::Right, "Right"}, {Key::Left, "Left"}, {Key::Down, "Down"}, {Key::Up, "Up"},
    {Key::PageUp, "PageUp"}, {Key::PageDown, "PageDown"}, {Key::Home, "Home"}, {Key::End, "End"},
    {Key::F1, "F1"}, {Key::F2, "F2"}, {Key::F3, "F3"}, {Key::F4, "F4"}, {Key::F5, "F5"},
    {Key::F6, "F6"}, {Key::F7, "F7"}, {Key::F8, "F8"}, {Key::F9, "F9"}, {Key::F10, "F10"},
    {Key::F11, "F11"}, {Key::F12, "F12"},
    {Key::LeftShift, "LeftShift"}, {Key::LeftControl, "LeftControl"}, {Key::LeftAlt, "LeftAlt"},
    {Key::RightShift, "RightShift"}, {Key::RightControl, "RightControl"}, {Key::RightAlt, "RightAlt"},
};

constexpr std::pair<GamepadButton, const char*> kGamepadButtonNames[] = {
    {GamepadButton::A, "A"}, {GamepadButton::B, "B"}, {GamepadButton::X, "X"}, {GamepadButton::Y, "Y"},
    {GamepadButton::LeftBumper, "LeftBumper"}, {GamepadButton::RightBumper, "RightBumper"},
    {GamepadButton::Back, "Back"}, {GamepadButton::Start, "Start"}, {GamepadButton::Guide, "Guide"},
    {GamepadButton::LeftThumb, "LeftThumb"}, {GamepadButton::RightThumb, "RightThumb"},
    {GamepadButton::DPadUp, "DPadUp"}, {GamepadButton::DPadRight, "DPadRight"},
    {GamepadButton::DPadDown, "DPadDown"}, {GamepadButton::DPadLeft, "DPadLeft"},
};

constexpr std::pair<GamepadAxis, const char*> kGamepadAxisNames[] = {
    {GamepadAxis::LeftX, "LeftX"}, {GamepadAxis::LeftY, "LeftY"},
    {GamepadAxis::RightX, "RightX"}, {GamepadAxis::RightY, "RightY"},
    {GamepadAxis::LeftTrigger, "LeftTrigger"}, {GamepadAxis::RightTrigger, "RightTrigger"},
};

// 全表状态（函数内静态：首次访问时构造并填入内置默认绑定）
struct State {
    std::vector<std::string> actions;                   // 动作名（保持插入顺序）
    std::vector<std::vector<InputBinding>> actionBindings;  // 与 actions 同下标
    std::vector<std::string> axes;                      // 轴名（保持插入顺序）
    std::vector<std::vector<AxisBinding>> axisBindings;
    std::set<std::string> warnedMissing;                // 缺失名 warn 一次集合（进程生命周期）

    State() { setDefaults(); }

    // 内置默认绑定（load 失败/未调用时生效，保证「无手柄回退键鼠」开箱可用）
    void setDefaults() {
        actions.clear();
        actionBindings.clear();
        axes.clear();
        axisBindings.clear();
        {   // Horizontal：A/D + 左摇杆 X
            AxisBinding kbd;
            kbd.negativeKey = Key::A;
            kbd.positiveKey = Key::D;
            AxisBinding pad;
            pad.useGamepadAxis = true;
            pad.gamepadAxis = GamepadAxis::LeftX;
            axes.push_back("Horizontal");
            axisBindings.push_back({kbd, pad});
        }
        {   // Vertical：S/W + 左摇杆 Y（GLFW 上推为负 → scale -1 反转为上正）
            AxisBinding kbd;
            kbd.negativeKey = Key::S;
            kbd.positiveKey = Key::W;
            AxisBinding pad;
            pad.useGamepadAxis = true;
            pad.gamepadAxis = GamepadAxis::LeftY;
            pad.gamepadScale = -1.0f;
            axes.push_back("Vertical");
            axisBindings.push_back({kbd, pad});
        }
        {   // Jump：Space + 手柄 A
            InputBinding kbd;
            kbd.key = Key::Space;
            InputBinding pad;
            pad.useGamepadButton = true;
            pad.gamepadButton = GamepadButton::A;
            actions.push_back("Jump");
            actionBindings.push_back({kbd, pad});
        }
        addDefaultAction("Submit", Key::Enter);
        addDefaultAction("Cancel", Key::Escape);
        addDefaultAction("Pause", Key::P);
    }

    void addDefaultAction(const std::string& name, Key key) {
        InputBinding b;
        b.key = key;
        actions.push_back(name);
        actionBindings.push_back({b});
    }
};

State& state() {
    static State s;
    return s;
}

int findAction(const std::string& name) {
    const auto& names = state().actions;
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

int findAxis(const std::string& name) {
    const auto& names = state().axes;
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

void warnMissingOnce(const std::string& name) {
    if (state().warnedMissing.insert(name).second)
        Logger::warn("[InputMap] 未知动作/轴: {}", name);
}

bool ctrlHeld() { return Input::isKeyDown(Key::LeftControl) || Input::isKeyDown(Key::RightControl); }
bool shiftHeld() { return Input::isKeyDown(Key::LeftShift) || Input::isKeyDown(Key::RightShift); }
bool altHeld() { return Input::isKeyDown(Key::LeftAlt) || Input::isKeyDown(Key::RightAlt); }
bool modsSatisfied(bool ctrl, bool shift, bool alt) {
    return (!ctrl || ctrlHeld()) && (!shift || shiftHeld()) && (!alt || altHeld());
}

// 动作绑定求值；mode：0=按住 1=本帧按下 2=本帧松开
bool bindingActive(const InputBinding& b, int mode) {
    // 键鼠源：键或鼠标任一触发（多源并存），修饰键须按住
    bool kbd = false;
    if (b.key != Key::Unknown) {
        kbd = mode == 0 ? Input::isKeyDown(b.key)
            : mode == 1 ? Input::isKeyPressed(b.key)
                        : Input::isKeyReleased(b.key);
    }
    if (!kbd && b.useMouseButton) {
        kbd = mode == 0 ? Input::isMouseDown(b.mouseButton)
            : mode == 1 ? Input::isMousePressed(b.mouseButton)
                        : Input::isMouseReleased(b.mouseButton);
    }
    if (kbd && modsSatisfied(b.ctrl, b.shift, b.alt)) return true;
    // 手柄源：忽略修饰；手柄未连接时 Input 查询恒 false，自动回退键鼠
    if (b.useGamepadButton) {
        const bool gp = mode == 0 ? Input::isGamepadButtonDown(b.gamepadButton, b.gamepadIndex)
            : mode == 1 ? Input::isGamepadButtonPressed(b.gamepadButton, b.gamepadIndex)
                        : Input::isGamepadButtonReleased(b.gamepadButton, b.gamepadIndex);
        if (gp) return true;
    }
    return false;
}

const char* mouseButtonName(MouseButton m) {
    if (m == MouseButton::Right) return "Right";
    if (m == MouseButton::Middle) return "Middle";
    return "Left";
}

// ===== JSON 序列化（只写非缺省字段；缺省：ctrl/shift/alt=false、gamepadIndex=0、gamepadScale=1.0、
//          negativeKey/positiveKey=无、gamepadAxis 缺省=不用）=====

nlohmann::json bindingToJson(const InputBinding& b) {
    nlohmann::json j = nlohmann::json::object();
    if (b.key != Key::Unknown) j["key"] = InputMap::keyName(b.key);
    if (b.useMouseButton) j["mouseButton"] = mouseButtonName(b.mouseButton);
    if (b.useGamepadButton) j["gamepadButton"] = InputMap::gamepadButtonName(b.gamepadButton);
    if (b.gamepadIndex != 0) j["gamepadIndex"] = b.gamepadIndex;
    if (b.ctrl) j["ctrl"] = true;
    if (b.shift) j["shift"] = true;
    if (b.alt) j["alt"] = true;
    return j;
}

nlohmann::json axisBindingToJson(const AxisBinding& b) {
    nlohmann::json j = nlohmann::json::object();
    if (b.negativeKey != Key::Unknown) j["negativeKey"] = InputMap::keyName(b.negativeKey);
    if (b.positiveKey != Key::Unknown) j["positiveKey"] = InputMap::keyName(b.positiveKey);
    if (b.useGamepadAxis) j["gamepadAxis"] = InputMap::gamepadAxisName(b.gamepadAxis);
    if (b.gamepadScale != 1.0f) j["gamepadScale"] = b.gamepadScale;
    if (b.ctrl) j["ctrl"] = true;
    if (b.shift) j["shift"] = true;
    if (b.alt) j["alt"] = true;
    return j;
}

// 解析失败（未知键名/坏条目）→ warn + nullopt，调用方跳过该条
std::optional<InputBinding> parseActionBinding(const nlohmann::json& e) {
    if (!e.is_object()) {
        Logger::warn("[InputMap] 动作绑定条目不是对象，跳过该条");
        return std::nullopt;
    }
    InputBinding b;
    bool hasSource = false;
    if (e.contains("key")) {
        if (!e["key"].is_string()) {
            Logger::warn("[InputMap] 绑定 key 字段不是字符串，跳过该条");
            return std::nullopt;
        }
        const std::string s = e["key"].get<std::string>();
        const Key k = InputMap::keyFromName(s);
        if (k == Key::Unknown) {
            Logger::warn("[InputMap] 未知键名 \"{}\"，跳过该条", s);
            return std::nullopt;
        }
        b.key = k;
        hasSource = true;
    }
    if (e.contains("mouseButton")) {
        if (!e["mouseButton"].is_string()) {
            Logger::warn("[InputMap] 绑定 mouseButton 字段不是字符串，跳过该条");
            return std::nullopt;
        }
        const std::string s = e["mouseButton"].get<std::string>();
        if (s == "Left") b.mouseButton = MouseButton::Left;
        else if (s == "Right") b.mouseButton = MouseButton::Right;
        else if (s == "Middle") b.mouseButton = MouseButton::Middle;
        else {
            Logger::warn("[InputMap] 未知 mouseButton \"{}\"，跳过该条", s);
            return std::nullopt;
        }
        b.useMouseButton = true;
        hasSource = true;
    }
    if (e.contains("gamepadButton")) {
        if (!e["gamepadButton"].is_string()) {
            Logger::warn("[InputMap] 绑定 gamepadButton 字段不是字符串，跳过该条");
            return std::nullopt;
        }
        const std::string s = e["gamepadButton"].get<std::string>();
        const GamepadButton g = InputMap::gamepadButtonFromName(s);
        if (g == GamepadButton::Count) {
            Logger::warn("[InputMap] 未知 gamepadButton \"{}\"，跳过该条", s);
            return std::nullopt;
        }
        b.gamepadButton = g;
        b.useGamepadButton = true;
        hasSource = true;
    }
    if (!hasSource) {
        Logger::warn("[InputMap] 绑定无任何输入源（key/mouseButton/gamepadButton），跳过该条");
        return std::nullopt;
    }
    // 可选字段：类型不符按缺省处理
    if (e.contains("gamepadIndex") && e["gamepadIndex"].is_number_integer())
        b.gamepadIndex = std::clamp(e["gamepadIndex"].get<int>(), 0, Input::kMaxGamepads - 1);
    if (e.contains("ctrl") && e["ctrl"].is_boolean()) b.ctrl = e["ctrl"].get<bool>();
    if (e.contains("shift") && e["shift"].is_boolean()) b.shift = e["shift"].get<bool>();
    if (e.contains("alt") && e["alt"].is_boolean()) b.alt = e["alt"].get<bool>();
    return b;
}

std::optional<AxisBinding> parseAxisBinding(const nlohmann::json& e) {
    if (!e.is_object()) {
        Logger::warn("[InputMap] 轴绑定条目不是对象，跳过该条");
        return std::nullopt;
    }
    AxisBinding b;
    bool hasSource = false;
    if (e.contains("negativeKey")) {
        if (!e["negativeKey"].is_string()) {
            Logger::warn("[InputMap] 绑定 negativeKey 字段不是字符串，跳过该条");
            return std::nullopt;
        }
        const std::string s = e["negativeKey"].get<std::string>();
        const Key k = InputMap::keyFromName(s);
        if (k == Key::Unknown) {
            Logger::warn("[InputMap] 未知键名 \"{}\"，跳过该条", s);
            return std::nullopt;
        }
        b.negativeKey = k;
        hasSource = true;
    }
    if (e.contains("positiveKey")) {
        if (!e["positiveKey"].is_string()) {
            Logger::warn("[InputMap] 绑定 positiveKey 字段不是字符串，跳过该条");
            return std::nullopt;
        }
        const std::string s = e["positiveKey"].get<std::string>();
        const Key k = InputMap::keyFromName(s);
        if (k == Key::Unknown) {
            Logger::warn("[InputMap] 未知键名 \"{}\"，跳过该条", s);
            return std::nullopt;
        }
        b.positiveKey = k;
        hasSource = true;
    }
    if (e.contains("gamepadAxis")) {
        if (!e["gamepadAxis"].is_string()) {
            Logger::warn("[InputMap] 绑定 gamepadAxis 字段不是字符串，跳过该条");
            return std::nullopt;
        }
        const std::string s = e["gamepadAxis"].get<std::string>();
        const GamepadAxis a = InputMap::gamepadAxisFromName(s);
        if (a == GamepadAxis::Count) {
            Logger::warn("[InputMap] 未知 gamepadAxis \"{}\"，跳过该条", s);
            return std::nullopt;
        }
        b.gamepadAxis = a;
        b.useGamepadAxis = true;
        hasSource = true;
    }
    if (!hasSource) {
        Logger::warn("[InputMap] 轴绑定无任何输入源（negativeKey/positiveKey/gamepadAxis），跳过该条");
        return std::nullopt;
    }
    // 可选字段：类型不符按缺省处理
    if (e.contains("gamepadScale") && e["gamepadScale"].is_number())
        b.gamepadScale = e["gamepadScale"].get<float>();
    if (e.contains("ctrl") && e["ctrl"].is_boolean()) b.ctrl = e["ctrl"].get<bool>();
    if (e.contains("shift") && e["shift"].is_boolean()) b.shift = e["shift"].get<bool>();
    if (e.contains("alt") && e["alt"].is_boolean()) b.alt = e["alt"].get<bool>();
    return b;
}

}  // namespace

void InputMap::beginFrame() {
    // 预留：全部查询实时读取 Input 每帧状态（绑定改动即时生效），无需帧内缓存。
    // 由整合者在 editor/player 主循环紧随 Input::beginFrame 之后调用（契约 §24.5）。
}

bool InputMap::load(const std::filesystem::path& path) {
    std::string content;
    if (!AssetStream::readAllText(path, content)) {
        Logger::warn("[InputMap] 读取失败（保留现有绑定）: {}", path.string());
        return false;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(content);
    } catch (const std::exception& e) {
        Logger::warn("[InputMap] JSON 解析失败（保留现有绑定）{}: {}", path.string(), e.what());
        return false;
    }
    if (!root.is_object() || !root.contains("version") || !root["version"].is_number_integer() ||
        root["version"].get<int>() != 1) {
        Logger::warn("[InputMap] version 缺失或不受支持（保留现有绑定）: {}", path.string());
        return false;
    }

    // 先解析到临时表，全部成功才整表替换（load = 整表替换；任何失败保留旧表）
    std::vector<std::string> newActions;
    std::vector<std::vector<InputBinding>> newActionBindings;
    if (root.contains("actions")) {
        if (!root["actions"].is_object()) {
            Logger::warn("[InputMap] actions 段不是对象（保留现有绑定）: {}", path.string());
            return false;
        }
        for (auto it = root["actions"].begin(); it != root["actions"].end(); ++it) {
            const std::string name = it.key();
            if (name.empty()) {
                Logger::warn("[InputMap] 动作名为空，忽略该条目");
                continue;
            }
            if (!it.value().is_array()) {
                Logger::warn("[InputMap] 动作 {} 的绑定不是数组，忽略该条目", name);
                continue;
            }
            std::vector<InputBinding> bindings;
            for (const nlohmann::json& e : it.value()) {
                if (auto b = parseActionBinding(e)) bindings.push_back(*b);
            }
            newActions.push_back(name);
            newActionBindings.push_back(std::move(bindings));
        }
    }

    std::vector<std::string> newAxes;
    std::vector<std::vector<AxisBinding>> newAxisBindings;
    if (root.contains("axes")) {
        if (!root["axes"].is_object()) {
            Logger::warn("[InputMap] axes 段不是对象（保留现有绑定）: {}", path.string());
            return false;
        }
        for (auto it = root["axes"].begin(); it != root["axes"].end(); ++it) {
            const std::string name = it.key();
            if (name.empty()) {
                Logger::warn("[InputMap] 轴名为空，忽略该条目");
                continue;
            }
            if (!it.value().is_array()) {
                Logger::warn("[InputMap] 轴 {} 的绑定不是数组，忽略该条目", name);
                continue;
            }
            std::vector<AxisBinding> bindings;
            for (const nlohmann::json& e : it.value()) {
                if (auto b = parseAxisBinding(e)) bindings.push_back(*b);
            }
            newAxes.push_back(name);
            newAxisBindings.push_back(std::move(bindings));
        }
    }

    state().actions = std::move(newActions);
    state().actionBindings = std::move(newActionBindings);
    state().axes = std::move(newAxes);
    state().axisBindings = std::move(newAxisBindings);
    Logger::info("[InputMap] 已加载输入绑定（{} 动作 / {} 轴）: {}",
                 state().actions.size(), state().axes.size(), path.string());
    return true;
}

bool InputMap::save(const std::filesystem::path& path) {
    nlohmann::json root = nlohmann::json::object();
    root["version"] = 1;
    nlohmann::json acts = nlohmann::json::object();
    for (size_t i = 0; i < state().actions.size(); ++i) {
        nlohmann::json arr = nlohmann::json::array();
        for (const InputBinding& b : state().actionBindings[i]) arr.push_back(bindingToJson(b));
        acts[state().actions[i]] = std::move(arr);
    }
    root["actions"] = std::move(acts);
    nlohmann::json axs = nlohmann::json::object();
    for (size_t i = 0; i < state().axes.size(); ++i) {
        nlohmann::json arr = nlohmann::json::array();
        for (const AxisBinding& b : state().axisBindings[i]) arr.push_back(axisBindingToJson(b));
        axs[state().axes[i]] = std::move(arr);
    }
    root["axes"] = std::move(axs);

    // 写盘（编辑器侧用）；父目录不存在则先建（与 TextureMeta::save 同纪律）
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        Logger::warn("[InputMap] 写入失败: {}", path.string());
        return false;
    }
    out << root.dump(4) << '\n';
    if (!out.good()) {
        Logger::warn("[InputMap] 写入不完整: {}", path.string());
        return false;
    }
    return true;
}

void InputMap::clear() {
    state().actions.clear();
    state().actionBindings.clear();
    state().axes.clear();
    state().axisBindings.clear();
}

// ===== 脚本查询 =====

bool InputMap::hasAction(const std::string& name) {
    return findAction(name) >= 0;
}

bool InputMap::getAction(const std::string& name) {
    const int i = findAction(name);
    if (i < 0) {
        warnMissingOnce(name);
        return false;
    }
    for (const InputBinding& b : state().actionBindings[i])
        if (bindingActive(b, 0)) return true;
    return false;
}

bool InputMap::getActionDown(const std::string& name) {
    const int i = findAction(name);
    if (i < 0) {
        warnMissingOnce(name);
        return false;
    }
    for (const InputBinding& b : state().actionBindings[i])
        if (bindingActive(b, 1)) return true;
    return false;
}

bool InputMap::getActionUp(const std::string& name) {
    const int i = findAction(name);
    if (i < 0) {
        warnMissingOnce(name);
        return false;
    }
    for (const InputBinding& b : state().actionBindings[i])
        if (bindingActive(b, 2)) return true;
    return false;
}

float InputMap::getAxis(const std::string& name) {
    const int i = findAxis(name);
    if (i < 0) {
        warnMissingOnce(name);
        return 0.0f;
    }
    float best = 0.0f;
    for (const AxisBinding& b : state().axisBindings[i]) {
        // 修饰键不满足 → 绑定按 0 计
        if (!modsSatisfied(b.ctrl, b.shift, b.alt)) continue;
        // 键盘部分：(正键?1:0)-(负键?1:0)
        float kv = 0.0f;
        if (b.positiveKey != Key::Unknown && Input::isKeyDown(b.positiveKey)) kv += 1.0f;
        if (b.negativeKey != Key::Unknown && Input::isKeyDown(b.negativeKey)) kv -= 1.0f;
        if (std::abs(kv) > std::abs(best)) best = kv;
        // 手柄部分：gamepadAxis × gamepadScale（死区已由 Input::gamepadAxis 全局应用）
        if (b.useGamepadAxis && Input::isGamepadConnected()) {
            const float gv = Input::gamepadAxis(b.gamepadAxis) * b.gamepadScale;
            if (std::abs(gv) > std::abs(best)) best = gv;
        }
    }
    // 契约签名 -1..1：gamepadScale 可放大（绑定面板上限 ±8），最终结果钳制（Reviewer 修复）
    return std::clamp(best, -1.0f, 1.0f);
}

// ===== 绑定管理 =====

const std::vector<std::string>& InputMap::actionNames() { return state().actions; }
const std::vector<std::string>& InputMap::axisNames() { return state().axes; }

void InputMap::addAction(const std::string& name) {
    if (findAction(name) >= 0) {
        Logger::warn("[InputMap] 动作已存在，忽略添加: {}", name);
        return;
    }
    state().actions.push_back(name);
    state().actionBindings.push_back({});
}

void InputMap::removeAction(const std::string& name) {
    const int i = findAction(name);
    if (i < 0) {
        Logger::warn("[InputMap] 动作不存在，忽略移除: {}", name);
        return;
    }
    state().actions.erase(state().actions.begin() + i);
    state().actionBindings.erase(state().actionBindings.begin() + i);
}

void InputMap::addAxis(const std::string& name) {
    if (findAxis(name) >= 0) {
        Logger::warn("[InputMap] 轴已存在，忽略添加: {}", name);
        return;
    }
    state().axes.push_back(name);
    state().axisBindings.push_back({});
}

void InputMap::removeAxis(const std::string& name) {
    const int i = findAxis(name);
    if (i < 0) {
        Logger::warn("[InputMap] 轴不存在，忽略移除: {}", name);
        return;
    }
    state().axes.erase(state().axes.begin() + i);
    state().axisBindings.erase(state().axisBindings.begin() + i);
}

void InputMap::setActionBindings(const std::string& name, const std::vector<InputBinding>& b) {
    const int i = findAction(name);
    if (i < 0) {
        Logger::warn("[InputMap] 动作不存在，忽略绑定设置: {}", name);
        return;
    }
    state().actionBindings[i] = b;
}

void InputMap::setAxisBindings(const std::string& name, const std::vector<AxisBinding>& b) {
    const int i = findAxis(name);
    if (i < 0) {
        Logger::warn("[InputMap] 轴不存在，忽略绑定设置: {}", name);
        return;
    }
    state().axisBindings[i] = b;
}

const std::vector<InputBinding>& InputMap::actionBindings(const std::string& name) {
    static const std::vector<InputBinding> kEmpty;
    const int i = findAction(name);
    return i < 0 ? kEmpty : state().actionBindings[i];
}

const std::vector<AxisBinding>& InputMap::axisBindings(const std::string& name) {
    static const std::vector<AxisBinding> kEmpty;
    const int i = findAxis(name);
    return i < 0 ? kEmpty : state().axisBindings[i];
}

// ===== 键名 ↔ 枚举 =====

std::string InputMap::keyName(Key k) {
    for (const auto& [key, n] : kKeyNames)
        if (key == k) return n;
    return "";
}

Key InputMap::keyFromName(const std::string& name) {
    for (const auto& [key, n] : kKeyNames)
        if (name == n) return key;
    return Key::Unknown;
}

std::string InputMap::gamepadButtonName(GamepadButton b) {
    for (const auto& [btn, n] : kGamepadButtonNames)
        if (btn == b) return n;
    return "";
}

GamepadButton InputMap::gamepadButtonFromName(const std::string& name) {
    for (const auto& [btn, n] : kGamepadButtonNames)
        if (name == n) return btn;
    return GamepadButton::Count;   // 未知名哨兵（Count 非合法按钮，Input 查询恒 false）
}

std::string InputMap::gamepadAxisName(GamepadAxis a) {
    for (const auto& [axis, n] : kGamepadAxisNames)
        if (axis == a) return n;
    return "";
}

GamepadAxis InputMap::gamepadAxisFromName(const std::string& name) {
    for (const auto& [axis, n] : kGamepadAxisNames)
        if (name == n) return axis;
    return GamepadAxis::Count;   // 未知名哨兵（Count 非合法轴，Input 查询恒 0）
}

}  // namespace NovaEngine
