#pragma once
#include "../Panel.h"

#include <string>

namespace NovaEditor {

// 输入绑定面板（V0.5-11，第 9 面板；View 菜单 + dock 底部 Tab）：
// 编辑 assets/input/default.novainput（动作/轴列表增删 + 绑定列表编辑）。
// 改动即时调 InputMap::setActionBindings/setAxisBindings（Play 中查询即时生效）；
// Save 调 InputMap::save（父目录 assets/input/ 不存在自动创建）。简版 v1：无 Listen 录制。
class InputBindingsPanel : public Panel {
public:
    explicit InputBindingsPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    // 选中动作的绑定列表（每行单源：Key/MouseButton/GamepadButton + 键位 + 修饰键 + 删除）
    void drawActionBindings(const std::string& actionName);
    // 选中轴的绑定列表（negativeKey/positiveKey/gamepadAxis/gamepadScale + 修饰键 + 删除）
    void drawAxisBindings(const std::string& axisName);

    std::string m_configPath = "assets/input/default.novainput";  // 项目内相对路径
    int m_selectedAction = -1;   // 动作列表选中下标（-1 = 无选中）
    int m_selectedAxis = -1;     // 轴列表选中下标（-1 = 无选中）
};

}  // namespace NovaEditor
