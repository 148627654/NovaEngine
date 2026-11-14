#pragma once
#include "../Panel.h"

#include <NovaEngine/Render/Framebuffer.h>

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace NovaEngine {
class GameObject;
}  // namespace NovaEngine

namespace NovaEditor {

class GizmoController;

// 场景视口（§3.2）：FBO 离屏渲染 → ImGui::Image 显示 → 拾取选择 → gizmo → 拖放导入
// 每帧整帧 3D 渲染都在本面板 onGui 内完成（不在 EditorApplication::render 里画 3D）。
class SceneViewPanel : public Panel {
public:
    explicit SceneViewPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

    GizmoController* m_gizmo = nullptr;  // 由 EditorApplication 注入

private:
    void handlePicking(EditorContext& ctx, EditorCamera& camera,
                       const glm::vec2& vpPos, const glm::vec2& vpSize, bool hovered);
    void focusSelected(EditorContext& ctx, EditorCamera& camera);
    void focusGameObject(EditorCamera& camera, NovaEngine::GameObject* go);
    // Play 态顶部工具栏（V0.2-13/V0.3-10 子集）：分辨率预设 / Render Scale / Follow Selected
    void drawPlayToolbar(EditorContext& ctx);
    // Follow Selected：把主相机 Transform 设到 选中对象世界位置 + 记录偏移（朝向不变）
    void applyFollowSelected(EditorContext& ctx);

    NovaEngine::Framebuffer m_fbo;
    bool m_fboCreated = false;
    bool m_pickArmed = false;          // 左键按下待释放判定（位移阈值 3px）
    glm::vec2 m_pickPressPos{0.0f};
    // Follow Selected 状态（V0.3-10 子集）：开关/选中变化/退出 Play 时重置偏移
    bool m_followOffsetValid = false;
    glm::vec3 m_followOffset{0.0f};
};

}  // namespace NovaEditor
