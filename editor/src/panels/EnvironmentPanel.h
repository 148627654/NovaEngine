#pragma once
#include "../Panel.h"

#include <string>

namespace NovaEngine {
class Scene;
}  // namespace NovaEngine

namespace NovaEditor {

// 环境面板（V0.2-5 收官）：基础环境（skyColor/ambientColor/showGrid/fogColor + "Reset Fog to Sky"）
// + 天空全组（V0.2-7：skyMode/Horizon/Zenith/Cubemap Path/Rotation/Exposure）。
// 原 Inspector 无选中时的 Environment 折叠区职责全部迁移至此；
// dockRight 区与 Inspector 并排 Tab（EnvironmentPanel 默认 open）。
// V1.0-4（§27.3）：Color Grading 折叠区——LUT（拖放/手输 + intensity）、DOF 简版、
// 渐晕/颗粒；全部写 scene->postProcess（environment.postProcess 段序列化）。
// 全部编辑走 ctx.markDirtyOnly / markSceneDirty 脏标记漏斗。
class EnvironmentPanel : public Panel {
public:
    explicit EnvironmentPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    std::string m_lutStatus;      // LUT 加载状态文本
    // P2-8（Reviewer）：状态与路径绑定——每帧与 settings.lutPath diff，场景切换/路径变化
    // 即重算（对当前路径调一次 setLut 拿真实结果），杜绝陈旧状态文本
    std::string m_lutStatusPath;  // m_lutStatus 对应的 lutPath
};

}  // namespace NovaEditor
