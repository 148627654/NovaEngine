#pragma once
#include "../Panel.h"

namespace NovaEditor {

// 动画状态机面板（V0.5-9，c7）：列表式配置——状态列表/参数列表/过渡列表（无节点图，
// ROADMAP 裁定）。作用于当前选中 GameObject 的 Animator 组件；无 Animator → 提示。
class AnimatorPanel : public Panel {
public:
    explicit AnimatorPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;
};

}  // namespace NovaEditor
