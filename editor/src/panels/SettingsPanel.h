#pragma once
#include "../Panel.h"

#include <string>
#include <utility>
#include <vector>

namespace NovaEditor {

// 编辑器设置面板（V1.0-12，§28.2）：
// 主题（2–3 套预置 + ImGuiStyle 自定义）/ 快捷键重绑（核心动作静态表 + 冲突检测）/
// 多视口开关（重启生效）。吸附值继续留在 Inspector 无选中时的 Snap Settings 折叠区
//（同源读写，落盘由 EditorApplication 的「运行态 → 设置」同步完成），此处不重复。
class SettingsPanel : public Panel {
public:
    SettingsPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    // 动作编辑缓冲（与 EditorSettings::defaultKeybindings 顺序对齐；首帧初始化一次；
    // P2-3：未编辑时每帧从当前生效绑定同步——重绑后显示生效值而非默认值）
    std::vector<std::pair<std::string, char[64]>> m_chordBufs;
    bool m_chordEditing = false;   // 任一 chord 输入框正在编辑（暂停同步防覆盖输入）
};

}  // namespace NovaEditor
