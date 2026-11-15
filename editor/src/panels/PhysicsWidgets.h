#pragma once
// PhysicsWidgets.h —— 物理组件 Inspector 编辑控件（V0.3-1，Coder A 提供）
//
// Coder C 在 InspectorPanel 的组件分发中调用（本头为唯一由 A 交付的 editor 文件）：
//     drawPhysicsComponents(ctx, comp, ctx.state == EditorContext::EditorState::Edit);
// dynamic_cast 分发 Rigidbody / BoxCollider / SphereCollider / CapsuleCollider / MeshCollider；
// 不匹配的类型直接 return（C 继续走其它分发，如脚本字段编辑）。
//
// 脏标记漏斗（V0.2-2 裁定 4）：拖拽/输入中间帧 markDirtyOnly；编辑结束
// （IsItemDeactivatedAfterEdit）或瞬时控件（Checkbox/InputText Enter）markSceneDirty。
// Play 态（editMode=false）：整组 BeginDisabled——物理参数在进入 Play 时已烘焙进 Jolt
// body（Scene::start → syncFromTransforms），Play 中改动不会生效，禁用避免误导。
//
// 说明：Edit 态修改形状/isTrigger 等参数不即时重建 body——Scene::start 的全量
// syncFromTransforms 保证进入 Play 时生效（与架构 §14.1 的「进 Play 重建」语义一致）。

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装

#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scene/Rigidbody.h>

#include <glm/gtc/type_ptr.hpp>

#include <cfloat>
#include <string>

namespace NovaEditor {

namespace PhysicsWidgetsDetail {

// 拖拽类控件共用漏斗：变化中 markDirtyOnly，松开（DeactivatedAfterEdit）markSceneDirty
inline void finishDragEdit(EditorContext& ctx) {
    if (ImGui::IsItemDeactivatedAfterEdit())
        ctx.markSceneDirty();
}

inline void dragFloat3Dirty(EditorContext& ctx, const char* label, glm::vec3& v, float speed,
                            float vMin = -FLT_MAX, float vMax = FLT_MAX) {
    if (ImGui::DragFloat3(label, glm::value_ptr(v), speed, vMin, vMax))
        ctx.markDirtyOnly();
    finishDragEdit(ctx);
}

inline void dragFloatDirty(EditorContext& ctx, const char* label, float& v, float speed,
                           float vMin = -FLT_MAX, float vMax = FLT_MAX) {
    if (ImGui::DragFloat(label, &v, speed, vMin, vMax))
        ctx.markDirtyOnly();
    finishDragEdit(ctx);
}

}  // namespace PhysicsWidgetsDetail

// Rigidbody：mass / useGravity / isKinematic / damping
inline void drawRigidbodyEditor(NovaEngine::Rigidbody& rb, EditorContext& ctx) {
    using namespace PhysicsWidgetsDetail;
    dragFloatDirty(ctx, "Mass", rb.mass, 0.01f, 0.01f, 1000.0f);
    if (ImGui::Checkbox("Use Gravity", &rb.useGravity))
        ctx.markSceneDirty();
    if (ImGui::Checkbox("Is Kinematic", &rb.isKinematic))
        ctx.markSceneDirty();
    dragFloatDirty(ctx, "Linear Damping", rb.linearDamping, 0.01f, 0.0f, 10.0f);
    dragFloatDirty(ctx, "Angular Damping", rb.angularDamping, 0.01f, 0.0f, 10.0f);
}

// Collider 公共字段：isTrigger / center
inline void drawColliderCommonEditor(NovaEngine::Collider& c, EditorContext& ctx) {
    using namespace PhysicsWidgetsDetail;
    if (ImGui::Checkbox("Is Trigger", &c.isTrigger))
        ctx.markSceneDirty();
    dragFloat3Dirty(ctx, "Center", c.center, 0.01f);
}

// 总入口：dynamic_cast 分发；不匹配直接 return（调用方继续其它分发）
inline void drawPhysicsComponents(EditorContext& ctx, NovaEngine::Component* comp, bool editMode) {
    using namespace NovaEngine;
    if (comp == nullptr)
        return;

    if (auto* rb = dynamic_cast<Rigidbody*>(comp)) {
        if (!editMode)
            ImGui::BeginDisabled();
        drawRigidbodyEditor(*rb, ctx);
        if (!editMode)
            ImGui::EndDisabled();
        return;
    }
    if (auto* box = dynamic_cast<BoxCollider*>(comp)) {
        if (!editMode)
            ImGui::BeginDisabled();
        drawColliderCommonEditor(*box, ctx);
        PhysicsWidgetsDetail::dragFloat3Dirty(ctx, "Half Extents", box->halfExtents, 0.01f,
                                              0.001f, 1000.0f);
        if (!editMode)
            ImGui::EndDisabled();
        return;
    }
    if (auto* sphere = dynamic_cast<SphereCollider*>(comp)) {
        if (!editMode)
            ImGui::BeginDisabled();
        drawColliderCommonEditor(*sphere, ctx);
        PhysicsWidgetsDetail::dragFloatDirty(ctx, "Radius", sphere->radius, 0.01f, 0.001f, 1000.0f);
        if (!editMode)
            ImGui::EndDisabled();
        return;
    }
    if (auto* capsule = dynamic_cast<CapsuleCollider*>(comp)) {
        if (!editMode)
            ImGui::BeginDisabled();
        drawColliderCommonEditor(*capsule, ctx);
        PhysicsWidgetsDetail::dragFloatDirty(ctx, "Radius", capsule->radius, 0.01f, 0.001f, 1000.0f);
        PhysicsWidgetsDetail::dragFloatDirty(ctx, "Height", capsule->height, 0.01f, 0.001f, 1000.0f);
        if (!editMode)
            ImGui::EndDisabled();
        return;
    }
    if (auto* mesh = dynamic_cast<MeshCollider*>(comp)) {
        if (!editMode)
            ImGui::BeginDisabled();
        drawColliderCommonEditor(*mesh, ctx);
        ImGui::TextUnformatted("Mesh Path (static/kinematic only)");
        std::string pathBuf = mesh->meshPath;
        if (ImGui::InputText("##MeshColliderPath", &pathBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
            mesh->meshPath = pathBuf;
            ctx.markSceneDirty();
        }
        if (!editMode)
            ImGui::EndDisabled();
        return;
    }
    // 非物理组件：直接返回
}

}  // namespace NovaEditor
