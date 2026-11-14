#pragma once
#include "../Panel.h"

namespace NovaEngine {
class GameObject;
class Scene;
}  // namespace NovaEngine

namespace NovaEditor {

// 属性面板（§6.5）：Transform 数值编辑 / 组件参数（材质、相机、灯光）/ 组件增删 /
// 脚本字段「伪反射」编辑（onSerialize → 逐键 ImGui 编辑 → onDeserialize 往返）/
// 无选中时的 Environment（天空）折叠区（V0.2-7）
// 脏标记漏斗（V0.2-2）：拖拽中 markDirtyOnly，编辑结束（IsItemDeactivatedAfterEdit）markSceneDirty
class InspectorPanel : public Panel {
public:
    explicit InspectorPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    void drawTransform(EditorContext& ctx, NovaEngine::GameObject* go);
    void drawMeshRenderer(EditorContext& ctx, NovaEngine::GameObject* go);
    void drawCamera(EditorContext& ctx, NovaEngine::GameObject* go);
    void drawDirectionalLight(EditorContext& ctx, NovaEngine::GameObject* go);
    // 返回是否发生了整场景重建（移除脚本走 JSON 重建，旧指针全部作废）
    bool drawScripts(EditorContext& ctx, NovaEngine::GameObject* go);
    void drawAddComponent(EditorContext& ctx, NovaEngine::GameObject* go);
    void drawEnvironment(EditorContext& ctx, NovaEngine::Scene* scene);
};

}  // namespace NovaEditor
