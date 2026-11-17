#include "InputBindingsPanel.h"

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Input.h>
#include <NovaEngine/Core/InputMap.h>
#include <NovaEngine/Core/Logger.h>

#include <string>
#include <utility>
#include <vector>

namespace NovaEditor {

namespace {

using namespace NovaEngine;

// 键位全表（懒建一次）：遍历 Key 值域 0..511，取 InputMap::keyName 非空者
//（Unknown = -1 不入表）。与引擎 kKeyNames 全表一致，免编辑器侧重复维护名单。
const std::vector<std::pair<int, std::string>>& allKeyNames() {
    static const std::vector<std::pair<int, std::string>> s = [] {
        std::vector<std::pair<int, std::string>> v;
        for (int i = 0; i < 512; ++i) {
            const std::string n = InputMap::keyName(static_cast<Key>(i));
            if (!n.empty()) v.emplace_back(i, n);
        }
        return v;
    }();
    return s;
}

// 手柄按钮名表（0..Count-1；Count 无名字不入表）
const std::vector<std::pair<int, std::string>>& allGamepadButtonNames() {
    static const std::vector<std::pair<int, std::string>> s = [] {
        std::vector<std::pair<int, std::string>> v;
        for (int i = 0; i < static_cast<int>(GamepadButton::Count); ++i) {
            const std::string n = InputMap::gamepadButtonName(static_cast<GamepadButton>(i));
            if (!n.empty()) v.emplace_back(i, n);
        }
        return v;
    }();
    return s;
}

// 手柄轴名表（0..Count-1）
const std::vector<std::pair<int, std::string>>& allGamepadAxisNames() {
    static const std::vector<std::pair<int, std::string>> s = [] {
        std::vector<std::pair<int, std::string>> v;
        for (int i = 0; i < static_cast<int>(GamepadAxis::Count); ++i) {
            const std::string n = InputMap::gamepadAxisName(static_cast<GamepadAxis>(i));
            if (!n.empty()) v.emplace_back(i, n);
        }
        return v;
    }();
    return s;
}

// string 表 → const char* 表（ImGui::Combo 用；生命周期内 strings 不得变更）
std::vector<const char*> toCstr(const std::vector<std::string>& v) {
    std::vector<const char*> out;
    out.reserve(v.size());
    for (const std::string& s : v) out.push_back(s.c_str());
    return out;
}

// Key → 全表下标（Unknown = -1）
int keyIndex(Key k) {
    const auto& all = allKeyNames();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].first == static_cast<int>(k)) return static_cast<int>(i);
    return -1;
}

// GamepadButton → 名表下标（越界兜底 0）
int gpButtonIndex(GamepadButton b) {
    const auto& all = allGamepadButtonNames();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].first == static_cast<int>(b)) return static_cast<int>(i);
    return 0;
}

// GamepadAxis → 名表下标（越界兜底 0）
int gpAxisIndex(GamepadAxis a) {
    const auto& all = allGamepadAxisNames();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].first == static_cast<int>(a)) return static_cast<int>(i);
    return 0;
}

}  // namespace

InputBindingsPanel::InputBindingsPanel() : Panel("Input Bindings") {
    open = false;   // 默认关；File → Input Bindings... 或 View 菜单打开
}

void InputBindingsPanel::onGui(EditorContext&, EditorCamera&) {
    using namespace NovaEngine;
    ImGui::Begin(m_name.c_str(), &open);

    // 顶部：配置路径（项目内相对路径）+ Reload / Save
    {
        std::string pathBuf = m_configPath;
        if (ImGui::InputText("Config Path", &pathBuf, ImGuiInputTextFlags_EnterReturnsTrue))
            m_configPath = pathBuf;
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            if (!InputMap::load(AssetDatabase::projectRoot() / m_configPath))
                Logger::warn("输入绑定 Reload 失败（保留当前绑定）: {}", m_configPath);
            m_selectedAction = -1;   // 整表已替换：清选中防越界
            m_selectedAxis = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            // InputMap::save 父目录不存在时自动创建（assets/input/，契约 §24.1）
            if (InputMap::save(AssetDatabase::projectRoot() / m_configPath))
                Logger::info("输入绑定已保存: {}", m_configPath);
        }
        ImGui::TextDisabled("(相对项目根路径，默认 assets/input/default.novainput)");
    }
    ImGui::Separator();

    // ===== 动作区：列表（增删） + 选中动作的绑定列表 =====
    std::vector<std::string> actions = InputMap::actionNames();   // 拷贝：增删后引用不悬垂
    ImGui::TextUnformatted("Actions");
    if (ImGui::BeginChild("actions", ImVec2(0.0f, 90.0f), true)) {
        for (size_t i = 0; i < actions.size(); ++i) {
            const bool sel = static_cast<int>(i) == m_selectedAction;
            if (ImGui::Selectable(actions[i].c_str(), sel)) m_selectedAction = static_cast<int>(i);
        }
    }
    ImGui::EndChild();
    {
        static char kNewAction[64] = {};
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputText("##newAction", kNewAction, sizeof(kNewAction));
        ImGui::SameLine();
        if (ImGui::Button("Add Action") && kNewAction[0] != '\0') {
            InputMap::addAction(kNewAction);   // 重名内部 warn + 忽略
            m_selectedAction = static_cast<int>(InputMap::actionNames().size()) - 1;
            actions = InputMap::actionNames();   // 本帧刷新列表：新选中项绑定区当帧显示（不延迟一帧）
        }
        ImGui::SameLine();
        const bool canRemove =
            m_selectedAction >= 0 && m_selectedAction < static_cast<int>(actions.size());
        if (canRemove && ImGui::Button("Remove Action")) {
            InputMap::removeAction(actions[m_selectedAction]);
            m_selectedAction = -1;
        }
    }
    if (m_selectedAction >= 0 && m_selectedAction < static_cast<int>(actions.size()))
        drawActionBindings(actions[m_selectedAction]);

    ImGui::Separator();

    // ===== 轴区：列表（增删） + 选中轴的绑定列表 =====
    std::vector<std::string> axes = InputMap::axisNames();
    ImGui::TextUnformatted("Axes");
    if (ImGui::BeginChild("axes", ImVec2(0.0f, 90.0f), true)) {
        for (size_t i = 0; i < axes.size(); ++i) {
            const bool sel = static_cast<int>(i) == m_selectedAxis;
            if (ImGui::Selectable(axes[i].c_str(), sel)) m_selectedAxis = static_cast<int>(i);
        }
    }
    ImGui::EndChild();
    {
        static char kNewAxis[64] = {};
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputText("##newAxis", kNewAxis, sizeof(kNewAxis));
        ImGui::SameLine();
        if (ImGui::Button("Add Axis") && kNewAxis[0] != '\0') {
            InputMap::addAxis(kNewAxis);   // 重名内部 warn + 忽略
            m_selectedAxis = static_cast<int>(InputMap::axisNames().size()) - 1;
            axes = InputMap::axisNames();   // 本帧刷新列表：新选中项绑定区当帧显示（不延迟一帧）
        }
        ImGui::SameLine();
        const bool canRemove = m_selectedAxis >= 0 && m_selectedAxis < static_cast<int>(axes.size());
        if (canRemove && ImGui::Button("Remove Axis")) {
            InputMap::removeAxis(axes[m_selectedAxis]);
            m_selectedAxis = -1;
        }
    }
    if (m_selectedAxis >= 0 && m_selectedAxis < static_cast<int>(axes.size()))
        drawAxisBindings(axes[m_selectedAxis]);

    ImGui::End();
}

void InputBindingsPanel::drawActionBindings(const std::string& actionName) {
    using namespace NovaEngine;
    ImGui::PushID("actionBindings");   // 与轴区隔离 ID（动作/轴同名控件防碰撞）
    ImGui::TextDisabled("Bindings of \"%s\":", actionName.c_str());

    std::vector<InputBinding> bindings = InputMap::actionBindings(actionName);
    bool changed = false;
    int removeAt = -1;

    // 下拉表：键位全表 / 手柄按钮全表（每帧重建 cstr 视图，strings 局部稳定）
    const auto& keyNames = allKeyNames();
    std::vector<std::string> keyDisplays;
    keyDisplays.reserve(keyNames.size());
    for (const auto& [k, n] : keyNames) keyDisplays.push_back(n);
    const std::vector<const char*> keyCstr = toCstr(keyDisplays);

    const auto& gpNames = allGamepadButtonNames();
    std::vector<std::string> gpDisplays;
    gpDisplays.reserve(gpNames.size());
    for (const auto& [b, n] : gpNames) gpDisplays.push_back(n);
    const std::vector<const char*> gpCstr = toCstr(gpDisplays);

    static const char* kSources[] = {"Key", "Mouse Button", "Gamepad Button"};
    static const char* kMouseButtons[] = {"Left", "Right", "Middle"};

    for (size_t i = 0; i < bindings.size(); ++i) {
        InputBinding& b = bindings[i];
        ImGui::PushID(static_cast<int>(i));
        // 源类型：简版 v1 每行单一输入源（切换源时保留各源字段值，仅改参与标志）
        int src = b.useGamepadButton ? 2 : (b.useMouseButton ? 1 : 0);
        if (src == 0 && b.key == Key::Unknown) {
            b.key = Key::Space;   // 防空白下拉（默认空格）
            changed = true;
        }
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Source", &src, kSources, 3)) {
            b.useMouseButton = (src == 1);
            b.useGamepadButton = (src == 2);
            changed = true;
        }
        ImGui::SameLine();
        if (src == 2) {
            int idx = gpButtonIndex(b.gamepadButton);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Button", &idx, gpCstr.data(), static_cast<int>(gpCstr.size()))) {
                b.gamepadButton = static_cast<GamepadButton>(gpNames[idx].first);
                changed = true;
            }
        } else if (src == 1) {
            int mb = static_cast<int>(b.mouseButton);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Button", &mb, kMouseButtons, 3)) {
                b.mouseButton = static_cast<MouseButton>(mb);
                changed = true;
            }
        } else {
            int idx = keyIndex(b.key);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Key", &idx, keyCstr.data(), static_cast<int>(keyCstr.size()))) {
                b.key = static_cast<Key>(keyNames[idx].first);
                changed = true;
            }
        }
        // 修饰键勾选（手柄源忽略修饰——引擎侧语义）
        if (ImGui::Checkbox("Ctrl", &b.ctrl)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Shift", &b.shift)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Alt", &b.alt)) changed = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) removeAt = static_cast<int>(i);
        ImGui::PopID();
    }

    if (ImGui::Button("Add Binding")) {
        InputBinding nb;
        nb.key = Key::Space;
        bindings.push_back(nb);
        changed = true;
    }
    if (removeAt >= 0) {
        bindings.erase(bindings.begin() + removeAt);
        changed = true;
    }
    // 改动即时生效（契约 §24.5：修改即时调 setActionBindings）
    if (changed) InputMap::setActionBindings(actionName, bindings);
    ImGui::PopID();
}

void InputBindingsPanel::drawAxisBindings(const std::string& axisName) {
    using namespace NovaEngine;
    ImGui::PushID("axisBindings");   // 与动作区隔离 ID
    ImGui::TextDisabled("Bindings of \"%s\":", axisName.c_str());

    std::vector<AxisBinding> bindings = InputMap::axisBindings(axisName);
    bool changed = false;
    int removeAt = -1;

    // 下拉表："None" + 全表（负/正键与手柄轴允许为空）
    const auto& keyNames = allKeyNames();
    std::vector<std::string> keyDisplays{"None"};
    for (const auto& [k, n] : keyNames) keyDisplays.push_back(n);
    const std::vector<const char*> keyCstr = toCstr(keyDisplays);

    const auto& axNames = allGamepadAxisNames();
    std::vector<std::string> axDisplays{"None"};
    for (const auto& [a, n] : axNames) axDisplays.push_back(n);
    const std::vector<const char*> axCstr = toCstr(axDisplays);

    for (size_t i = 0; i < bindings.size(); ++i) {
        AxisBinding& b = bindings[i];
        ImGui::PushID(static_cast<int>(i));
        // 下标 0 = None（Unknown），1..n = 全表 keyNames[n-1]
        int neg = keyIndex(b.negativeKey) + 1;
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("Negative Key", &neg, keyCstr.data(), static_cast<int>(keyCstr.size()))) {
            b.negativeKey = (neg == 0) ? Key::Unknown : static_cast<Key>(keyNames[neg - 1].first);
            changed = true;
        }
        ImGui::SameLine();
        int pos = keyIndex(b.positiveKey) + 1;
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("Positive Key", &pos, keyCstr.data(), static_cast<int>(keyCstr.size()))) {
            b.positiveKey = (pos == 0) ? Key::Unknown : static_cast<Key>(keyNames[pos - 1].first);
            changed = true;
        }
        ImGui::SameLine();
        int ax = b.useGamepadAxis ? gpAxisIndex(b.gamepadAxis) + 1 : 0;
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("Gamepad Axis", &ax, axCstr.data(), static_cast<int>(axCstr.size()))) {
            b.useGamepadAxis = (ax > 0);
            if (ax > 0) b.gamepadAxis = static_cast<GamepadAxis>(axNames[ax - 1].first);
            changed = true;
        }
        if (b.useGamepadAxis) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Scale", &b.gamepadScale, 0.05f, -8.0f, 8.0f, "%.2f"))
                changed = true;
        }
        if (ImGui::Checkbox("Ctrl", &b.ctrl)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Shift", &b.shift)) changed = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Alt", &b.alt)) changed = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) removeAt = static_cast<int>(i);
        ImGui::PopID();
    }

    if (ImGui::Button("Add Binding")) {
        AxisBinding nb;
        nb.negativeKey = Key::A;
        nb.positiveKey = Key::D;
        bindings.push_back(nb);
        changed = true;
    }
    if (removeAt >= 0) {
        bindings.erase(bindings.begin() + removeAt);
        changed = true;
    }
    // 改动即时生效（契约 §24.5：修改即时调 setAxisBindings）
    if (changed) InputMap::setAxisBindings(axisName, bindings);
    ImGui::PopID();
}

}  // namespace NovaEditor
