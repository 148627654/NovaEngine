#include "GizmoController.h"

#include "EditorContext.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>

#include <glm/gtc/matrix_inverse.hpp>
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

}  // namespace

void GizmoController::setCamera(const glm::mat4& view, const glm::mat4& projection) {
    m_view = view;
    m_projection = projection;
}

void GizmoController::update(EditorContext& ctx, const glm::vec2& viewportPos, const glm::vec2& viewportSize) {
    m_using = false;
    m_over = false;

    // 仅 Edit 态 + 恰好单选时显示
    if (ctx.state != EditorContext::EditorState::Edit) return;
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return;
    NovaEngine::GameObject* go = ctx.activeObject();
    if (!go || !ctx.scene) return;
    NovaEngine::Transform* transform = go->transform();
    if (!transform) return;

    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());  // 覆盖在 ImGui::Image 之上
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

    // 关键（§11.7）：glm 列主序 vs ImGuizmo 行主序，必须转置后传递
    const glm::mat4 viewT = glm::transpose(m_view);
    const glm::mat4 projT = glm::transpose(m_projection);
    glm::mat4 world = transform->worldMatrix();
    glm::mat4 worldT = glm::transpose(world);

    ImGuizmo::Manipulate(glm::value_ptr(viewT), glm::value_ptr(projT),
                         toImGuizmoOperation(m_operation), toImGuizmoMode(m_space),
                         glm::value_ptr(worldT));

    if (ImGuizmo::IsUsing()) {
        // 转置回 glm 列主序，分解写回 Transform
        const glm::mat4 newWorld = glm::transpose(worldT);
        glm::vec3 scale(1.0f), position(0.0f), skew(0.0f);
        glm::vec4 perspective;
        glm::quat rotation = glm::identity<glm::quat>();

        if (go->parent() && go->parent()->transform()) {
            // 有父级：反解局部（local = inverse(parentWorld) * world）
            const glm::mat4 local =
                glm::inverse(go->parent()->transform()->worldMatrix()) * newWorld;
            glm::decompose(local, scale, rotation, position, skew, perspective);
        } else {
            glm::decompose(newWorld, scale, rotation, position, skew, perspective);
        }

        transform->setLocalPosition(position);
        transform->setLocalRotation(rotation);
        transform->setLocalScale(scale);
        ctx.sceneDirty = true;
        m_using = true;
    }
    m_over = ImGuizmo::IsOver();
}

void GizmoController::cycleOperation() {
    switch (m_operation) {
    case Operation::Translate: m_operation = Operation::Rotate; break;
    case Operation::Rotate:    m_operation = Operation::Scale;  break;
    case Operation::Scale:     m_operation = Operation::Translate; break;
    }
}

void GizmoController::toggleSpace() {
    m_space = (m_space == Space::Local) ? Space::World : Space::Local;
}

}  // namespace NovaEditor
