#pragma once
#include "../Panel.h"

namespace NovaEngine {
class Scene;
}  // namespace NovaEngine

namespace NovaEditor {

// 环境面板（V0.2-5 收官）：基础环境（skyColor/ambientColor/showGrid/fogColor + "Reset Fog to Sky"）
// + 天空全组（V0.2-7：skyMode/Horizon/Zenith/Cubemap Path/Rotation/Exposure）。
// 原 Inspector 无选中时的 Environment 折叠区职责全部迁移至此；
// dockRight 区与 Inspector 并排 Tab（EnvironmentPanel 默认 open）。
// 全部编辑走 ctx.markDirtyOnly / markSceneDirty 脏标记漏斗。
class EnvironmentPanel : public Panel {
public:
    explicit EnvironmentPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;
};

}  // namespace NovaEditor
