#include "GizmoController.h"

#include "EditorContext.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace NovaEditor {

namespace {

ImGuizmo::OPERATION toImGuizmoOperation(GizmoController::Operation op) {
    switch (op) {
    case GizmoController::Operation::Translate: return ImGuizmo::TRANSLATE;
    case GizmoController::Operation::Rotate:    return ImGuizmo::ROTATE;
    case GizmoController::Operation::Scale:     return ImGuizmo::SCALE;
    }
    return ImGuizmo::TRANSLATE;
}

ImGuizmo::MODE toImGuizmoMode(GizmoController::Space space) {
    return space == GizmoController::Space::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
}

const char* operationName(GizmoController::Operation op) {
    switch (op) {
    case GizmoController::Operation::Translate: return "移动 Translate";
    case GizmoController::Operation::Rotate:    return "旋转 Rotate";
    case GizmoController::Operation::Scale:     return "缩放 Scale";
    }
    return "?";
}

const char* spaceName(GizmoController::Space space) {
    return space == GizmoController::Space::Local ? "Local" : "World";
}

}  // namespace

void GizmoController::setCamera(const glm::mat4& view, const glm::mat4& projection) {
    m_view = view;
    m_projection = projection;
}

void GizmoController::update(EditorContext& ctx, const glm::vec2& viewportPos, const glm::vec2& viewportSize) {
    const bool wasUsing = m_wasUsing;   // 本帧开始时的拖拽状态（用于检测 true→false 瞬间）
    m_using = false;
    m_over = false;

    // 仅 Edit 态 + 恰好单选时显示
    NovaEngine::GameObject* go = (ctx.state == EditorContext::EditorState::Edit &&
                                  viewportSize.x > 0.0f && viewportSize.y > 0.0f && ctx.scene)
                                     ? ctx.activeObject()
                                     : nullptr;
    NovaEngine::Transform* transform = go ? go->transform() : nullptr;
    if (!go || !transform) {
        // 早退路径也要推进拖拽状态（Reviewer P2-2）：拖拽中对象被删/取消选中时立即结算快照，
        // 防止 m_wasUsing 残留 true 导致下一帧推 phantom 快照
        if (wasUsing) ctx.markSceneDirty();
        m_wasUsing = false;
        return;
    }

    // 必须用当前 SceneView 窗口的 drawlist（而非前台 drawlist）：
    // 1) gizmo 图元追加在本帧 ImGui::Image 之后，自然覆盖在视口之上；
    // 2) ImGuizmo 的悬停判定 IsHoveringWindow 依赖 drawlist 的 _OwnerName 与
    //    g.HoveredWindow 匹配；前台 drawlist 的 owner 永远不是当前面板，
    //    mbMouseOver 恒假 → HandleTranslation/Scale/Rotation 直接返回，gizmo 无法交互。
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

    // 矩阵直接传 glm::value_ptr，无需转置：ImGuizmo 的 float[16] 与 glm/OpenGL
    // 同为列主序内存布局（内部按行向量约定计算，与列主序输入恰好互相抵消）。
    // 显式转置会破坏平移并触发 "behind camera" 检测，令 gizmo 被静默裁掉不显示。
    glm::mat4 world = transform->worldMatrix();

    // 激活状态变化时打日志（选中对象/操作/空间三者任一变化输出一次，便于排查「gizmo 不显示」）
    const uint64_t goId = go->id();
    if (goId != m_lastGoId || m_operation != m_lastLoggedOp || m_space != m_lastLoggedSpace) {
        NovaEngine::Logger::info("Gizmo 显示: {}（{}，{}）", go->name(),
                                 operationName(m_operation), spaceName(m_space));
        m_lastGoId = goId;
        m_lastLoggedOp = m_operation;
        m_lastLoggedSpace = m_space;
    }

    ImGuizmo::Manipulate(glm::value_ptr(m_view), glm::value_ptr(m_projection),
                         toImGuizmoOperation(m_operation), toImGuizmoMode(m_space),
                         glm::value_ptr(world));

    if (ImGuizmo::IsUsing()) {
        // Manipulate 已把新矩阵写回 world（同一列主序内存布局），直接分解写回 Transform
        glm::vec3 scale(1.0f), position(0.0f), skew(0.0f);
        glm::vec4 perspective;
        glm::quat rotation = glm::identity<glm::quat>();

        if (go->parent() && go->parent()->transform()) {
            // 有父级：反解局部（local = inverse(parentWorld) * world）
            const glm::mat4 local =
                glm::inverse(go->parent()->transform()->worldMatrix()) * world;
            glm::decompose(local, scale, rotation, position, skew, perspective);
        } else {
            glm::decompose(world, scale, rotation, position, skew, perspective);
        }

        transform->setLocalPosition(position);
        transform->setLocalRotation(rotation);
        transform->setLocalScale(scale);
        ctx.markDirtyOnly();  // 拖拽中每帧只置脏，不推快照（防刷栈，V0.2-1）
        m_using = true;
    }
    m_over = ImGuizmo::IsOver();

    // 拖拽结束瞬间（IsUsing 由 true→false）推「变更后」快照
    if (wasUsing && !m_using) ctx.markSceneDirty();
    m_wasUsing = m_using;
}

void GizmoController::cycleOperation() {
    switch (m_operation) {
    case Operation::Translate: m_operation = Operation::Rotate; break;
    case Operation::Rotate:    m_operation = Operation::Scale;  break;
    case Operation::Scale:     m_operation = Operation::Translate; break;
    }
}

void GizmoController::setOperation(Operation op) {
    m_operation = op;
    NovaEngine::Logger::info("Gizmo 操作切换: {}", operationName(op));
}

void GizmoController::toggleSpace() {
    m_space = (m_space == Space::Local) ? Space::World : Space::Local;
    NovaEngine::Logger::info("Gizmo 空间切换: {}", spaceName(m_space));
}

}  // namespace NovaEditor
