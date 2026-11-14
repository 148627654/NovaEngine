#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

namespace NovaEditor {

class EditorContext;

// ImGuizmo 封装（§6.3）：
//   单选且 Edit 态时 Manipulate；IsUsing 时 glm::decompose 写回 Transform
//   （有父级时用 inverse(parentWorld) * world 反解局部 TRS）。
class GizmoController {
public:
    enum class Operation { Translate, Rotate, Scale };
    enum class Space { Local, World };

    // 每帧由 SceneView 设置当前渲染相机矩阵
    void setCamera(const glm::mat4& view, const glm::mat4& projection);

    // 每帧调用（在 ImGui::Image 之后，绘制进前景 drawlist 覆盖在视口之上）
    void update(EditorContext& ctx, const glm::vec2& viewportPos, const glm::vec2& viewportSize);

    bool isUsing() const { return m_using; }   // gizmo 正在拖拽（抑制相机与选择）
    bool isOver() const { return m_over; }     // 鼠标悬停 gizmo（抑制点击选择）

    void cycleOperation();   // W/E/R 循环
    void toggleSpace();      // X 切换 Local/World
    void setOperation(Operation op) { m_operation = op; }
    Operation operation() const { return m_operation; }
    Space space() const { return m_space; }

private:
    Operation m_operation = Operation::Translate;
    Space m_space = Space::World;
    bool m_using = false;
    bool m_over = false;
    glm::mat4 m_view{1.0f};
    glm::mat4 m_projection{1.0f};
};

}  // namespace NovaEditor
