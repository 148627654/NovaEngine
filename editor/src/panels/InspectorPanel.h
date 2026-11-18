#pragma once
#include "../Panel.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

namespace NovaEngine {
class GameObject;
class MeshRenderer;
class Scene;
class Transform;
}  // namespace NovaEngine

namespace NovaEditor {

// 属性面板（§6.5）：Transform 数值编辑 / 组件参数（材质、相机、灯光）/ 组件增删 /
// 脚本字段「伪反射」编辑（onSerialize → 逐键 ImGui 编辑 → onDeserialize 往返）/
// 多选批量编辑（V0.2-15）/ 无选中时 Snap Settings 折叠区（V0.2-12）。
// Environment（天空）职责已迁移至 EnvironmentPanel（V0.2-5）。
// 脏标记漏斗（V0.2-2）：拖拽中 markDirtyOnly，编辑结束（IsItemDeactivatedAfterEdit）markSceneDirty
class InspectorPanel : public Panel {
public:
    explicit InspectorPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    // overrides = 该组件的 override 差异键（序列化键名，点分路径）；nullptr = 无预制体上下文
    void drawTransform(EditorContext& ctx, NovaEngine::GameObject* go,
                       const std::vector<std::string>* overrides = nullptr);
    void drawMeshRenderer(EditorContext& ctx, NovaEngine::GameObject* go,
                          const std::vector<std::string>* overrides = nullptr);
    void drawCamera(EditorContext& ctx, NovaEngine::GameObject* go,
                    const std::vector<std::string>* overrides = nullptr);
    void drawDirectionalLight(EditorContext& ctx, NovaEngine::GameObject* go,
                              const std::vector<std::string>* overrides = nullptr);
    void drawLODGroup(EditorContext& ctx, NovaEngine::GameObject* go);   // V0.5-3 波 2 合波接线
    // 返回是否发生了整场景重建（移除脚本走 JSON 重建，旧指针全部作废）
    bool drawScripts(EditorContext& ctx, NovaEngine::GameObject* go,
                     const std::unordered_map<std::string, std::vector<std::string>>& overridesByType);
    // Prefab 区（V0.3-9 波 4）：Open 路径显示 / Apply / Revert All / override 列表（组件+字段粒度）
    void drawPrefabSection(EditorContext& ctx, NovaEngine::GameObject* go,
                           const std::unordered_map<std::string, std::vector<std::string>>& overridesByType);
    void drawAddComponent(EditorContext& ctx, NovaEngine::GameObject* go);

    // 多选批量编辑（V0.2-15）：selectedIds.size() > 1
    void drawMultiEdit(EditorContext& ctx, const std::vector<NovaEngine::GameObject*>& gos);
    // 多选逐轴编辑：全相等显示公共值；不等显示灰字 "(…)" 标记+缓冲 0，拖动把绝对值写全部对象。
    // 拖拽中 markDirtyOnly，结束（IsItemDeactivatedAfterEdit）统一单次 markSceneDirty
    void drawMultiVec3(EditorContext& ctx, const char* label,
                       const std::vector<NovaEngine::Transform*>& ts,
                       const std::function<glm::vec3(NovaEngine::Transform*)>& getter,
                       const std::function<void(NovaEngine::Transform*, const glm::vec3&)>& setter,
                       float speed, float vMin, float vMax);
    // 多选公共材质区：以第一个对象为编辑缓冲，逐字段应用到全部 MeshRenderer
    void drawMultiMaterial(EditorContext& ctx, const std::vector<NovaEngine::MeshRenderer*>& mrs);

    // 无选中时 Snap Settings 折叠区（V0.2-12；运行期参数，不序列化、不置脏）
    void drawSnapSettings(EditorContext& ctx);
};

}  // namespace NovaEditor
