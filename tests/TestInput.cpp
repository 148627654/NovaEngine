// TestInput.cpp —— 输入映射族（§26.1 覆盖清单 5：InputMap 默认绑定往返）
//
// 对 KNOWN-ISSUES：
//  - V0.5-11 交付项：内置默认绑定（4 动作 2 轴）开箱可用、.novainput 读写往返
//  - 绑定全字段（键鼠/手柄/修饰键）JSON 序列化无损
//
// 无 GL / 无窗口依赖：InputMap 为全静态配置表，与 Input 运行时状态无关。

#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Core/InputMap.h>

#include "TestCommon.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace NovaEngine;
using namespace NovaTest;

namespace {

bool bindingEqual(const InputBinding& a, const InputBinding& b) {
    return a.key == b.key && a.mouseButton == b.mouseButton &&
           a.useMouseButton == b.useMouseButton && a.gamepadButton == b.gamepadButton &&
           a.useGamepadButton == b.useGamepadButton && a.gamepadIndex == b.gamepadIndex &&
           a.ctrl == b.ctrl && a.shift == b.shift && a.alt == b.alt;
}

bool axisBindingEqual(const AxisBinding& a, const AxisBinding& b) {
    return a.negativeKey == b.negativeKey && a.positiveKey == b.positiveKey &&
           a.gamepadAxis == b.gamepadAxis && a.useGamepadAxis == b.useGamepadAxis &&
           a.gamepadScale == b.gamepadScale && a.ctrl == b.ctrl && a.shift == b.shift &&
           a.alt == b.alt;
}

bool bindingListsEqual(const std::vector<InputBinding>& a, const std::vector<InputBinding>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!bindingEqual(a[i], b[i]))
            return false;
    return true;
}

bool axisListsEqual(const std::vector<AxisBinding>& a, const std::vector<AxisBinding>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!axisBindingEqual(a[i], b[i]))
            return false;
    return true;
}

// 快照当前全表：动作/轴名（排序）+ 按名绑定映射，供 save→clear→load 往返比对。
// 注意：.novainput 以 JSON 对象存动作/轴名 → nlohmann std::map 键序（字母序）dump/load，
// 名称顺序不保证与内存插入序一致——名称按集合（排序）比较，绑定按名字逐项比较。
struct TableSnapshot {
    std::vector<std::string> actions;
    std::vector<std::string> axes;
    std::map<std::string, std::vector<InputBinding>> actionBinds;
    std::map<std::string, std::vector<AxisBinding>> axisBinds;
};

TableSnapshot snapshotTable() {
    TableSnapshot s;
    for (const std::string& name : InputMap::actionNames()) {
        s.actions.push_back(name);
        s.actionBinds[name] = InputMap::actionBindings(name);
    }
    for (const std::string& name : InputMap::axisNames()) {
        s.axes.push_back(name);
        s.axisBinds[name] = InputMap::axisBindings(name);
    }
    std::sort(s.actions.begin(), s.actions.end());
    std::sort(s.axes.begin(), s.axes.end());
    return s;
}

void requireTableEqual(const TableSnapshot& expect) {
    TableSnapshot actual = snapshotTable();
    REQUIRE(actual.actions == expect.actions);
    REQUIRE(actual.axes == expect.axes);
    REQUIRE(actual.actionBinds.size() == expect.actionBinds.size());
    for (const auto& [name, binds] : expect.actionBinds) {
        INFO("动作绑定: " << name);
        REQUIRE(actual.actionBinds.contains(name));
        REQUIRE(bindingListsEqual(binds, actual.actionBinds[name]));
    }
    REQUIRE(actual.axisBinds.size() == expect.axisBinds.size());
    for (const auto& [name, binds] : expect.axisBinds) {
        INFO("轴绑定: " << name);
        REQUIRE(actual.axisBinds.contains(name));
        REQUIRE(axisListsEqual(binds, actual.axisBinds[name]));
    }
}

}  // namespace

TEST_CASE("InputMap: default bindings save/clear/load roundtrip", "[input][inputmap]") {
    // 内置默认绑定：Horizontal/Vertical 两轴 + Jump/Submit/Cancel/Pause 四动作（§24.1）
    const TableSnapshot defaults = snapshotTable();
    REQUIRE(defaults.actions.size() == 4);
    REQUIRE(defaults.axes.size() == 2);

    TempDir tmp("nova-input");
    const std::filesystem::path file = tmp.path() / "defaults.novainput";
    REQUIRE(InputMap::save(file));

    InputMap::clear();
    REQUIRE(InputMap::actionNames().empty());
    REQUIRE(InputMap::axisNames().empty());

    REQUIRE(InputMap::load(file));
    requireTableEqual(defaults);
}

TEST_CASE("InputMap: full binding JSON roundtrip", "[input][inputmap]") {
    const TableSnapshot before = snapshotTable();

    // 动作绑定：键 + 修饰键 + 鼠标 + 手柄多源
    InputBinding full;
    full.key = Key::W;
    full.ctrl = true;
    full.shift = false;
    full.alt = true;
    full.mouseButton = MouseButton::Right;
    full.useMouseButton = true;
    full.gamepadButton = GamepadButton::DPadUp;
    full.useGamepadButton = true;
    full.gamepadIndex = 1;
    InputMap::setActionBindings("Jump", {full});

    // 轴绑定：正负键 + 手柄轴 + 反向缩放
    AxisBinding axis;
    axis.negativeKey = Key::Left;
    axis.positiveKey = Key::Right;
    axis.gamepadAxis = GamepadAxis::RightY;
    axis.useGamepadAxis = true;
    axis.gamepadScale = -2.5f;
    axis.ctrl = true;
    axis.shift = true;
    axis.alt = false;
    InputMap::setAxisBindings("Horizontal", {axis});

    TempDir tmp("nova-input-full");
    const std::filesystem::path file = tmp.path() / "full.novainput";
    REQUIRE(InputMap::save(file));

    InputMap::clear();
    REQUIRE(InputMap::load(file));
    const TableSnapshot after = snapshotTable();
    REQUIRE(after.actionBinds.contains("Jump"));
    REQUIRE(bindingEqual(after.actionBinds.at("Jump")[0], full));
    REQUIRE(after.axisBinds.contains("Horizontal"));
    REQUIRE(axisBindingEqual(after.axisBinds.at("Horizontal")[0], axis));

    // 收尾：恢复进入本用例前的表（防同进程后续用例受污染；逐用例进程时无影响）
    InputMap::clear();
    for (size_t i = 0; i < before.actions.size(); ++i)
        InputMap::addAction(before.actions[i]);
    for (size_t i = 0; i < before.axes.size(); ++i)
        InputMap::addAxis(before.axes[i]);
}

TEST_CASE("InputMap: action/axis add/remove and dedup", "[input][inputmap]") {
    const size_t actionCount = InputMap::actionNames().size();
    InputMap::addAction("Fire");
    REQUIRE(InputMap::hasAction("Fire"));
    REQUIRE(InputMap::actionNames().size() == actionCount + 1);

    InputMap::addAction("Fire");   // 重名：warn + 忽略
    REQUIRE(InputMap::actionNames().size() == actionCount + 1);

    InputMap::removeAction("Fire");
    REQUIRE_FALSE(InputMap::hasAction("Fire"));

    const size_t axisCount = InputMap::axisNames().size();
    InputMap::addAxis("Zoom");
    REQUIRE(InputMap::axisNames().size() == axisCount + 1);
    InputMap::removeAxis("Zoom");
    REQUIRE(InputMap::axisNames().size() == axisCount);
}

TEST_CASE("InputMap: key name roundtrip and unknown-name safety", "[input][inputmap]") {
    for (const Key k : {Key::Space, Key::A, Key::W, Key::Escape, Key::Right, Key::F5,
                        Key::LeftShift, Key::LeftControl, Key::LeftAlt}) {
        INFO("Key: " << static_cast<int>(k));
        REQUIRE(InputMap::keyFromName(InputMap::keyName(k)) == k);
    }
    for (const GamepadButton b : {GamepadButton::A, GamepadButton::DPadLeft, GamepadButton::Guide}) {
        REQUIRE(InputMap::gamepadButtonFromName(InputMap::gamepadButtonName(b)) == b);
    }
    for (const GamepadAxis a : {GamepadAxis::LeftX, GamepadAxis::RightTrigger}) {
        REQUIRE(InputMap::gamepadAxisFromName(InputMap::gamepadAxisName(a)) == a);
    }

    // 未知名：返回空表/0/false，不崩溃（warn 一次）
    REQUIRE(InputMap::keyFromName("NoSuchKey") == Key::Unknown);
    REQUIRE(InputMap::actionBindings("NoSuchAction").empty());
    REQUIRE(InputMap::axisBindings("NoSuchAxis").empty());
    REQUIRE(InputMap::getAction("NoSuchAction") == false);
    REQUIRE(InputMap::getAxis("NoSuchAxis") == 0.0f);
}

TEST_CASE("InputMap: failed load keeps old table", "[input][inputmap]") {
    const TableSnapshot before = snapshotTable();
    TempDir tmp("nova-input-miss");
    REQUIRE_FALSE(InputMap::load(tmp.path() / "missing.novainput"));
    requireTableEqual(before);
}
