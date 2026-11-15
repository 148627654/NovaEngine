#pragma once
// UIWidgets.h —— UI 组件 Inspector 编辑（V0.3-3）
//
// header-only inline（editor/CMakeLists 零改动）。InspectorPanel 逐组件调用
// drawUIComponents(ctx, comp, editMode)：dynamic_cast 分发 Canvas/Image/Text/Button，
// 非 UI 组件直接 return（多组件同 GO 时每组件各自 CollapsingHeader + PushID）。
//
// 脏标记漏斗（与 MaterialEditorWidgets 一致）：拖拽/输入中 markDirtyOnly；
// 编辑结束（IsItemDeactivatedAfterEdit）或瞬时变更（Enter 应用/移除组件）markSceneDirty。
// Play 态约定：数值/颜色 live edit 保留；路径输入与 Remove Component 禁用。

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装

#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/Canvas.h>
#include <NovaEngine/Scene/Image.h>
#include <NovaEngine/Scene/Text.h>

#include <glm/gtc/type_ptr.hpp>

namespace NovaEditor {

// 编辑结束推「变更后」快照（拖拽/输入中间帧已由 markDirtyOnly 置脏）
inline void uiDirtyOnEditEnd(EditorContext& ctx) {
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
}

// 路径行：Enter 应用（空 = 清除）；Play 态禁用（与既有 Inspector 路径行为一致）
inline bool drawUIPathRow(EditorContext& ctx, const char* label, std::string& path,
                          bool editMode) {
    if (!editMode) ImGui::BeginDisabled();
    std::string buf = path;
    const bool applied =
        ImGui::InputText(label, &buf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!editMode) ImGui::EndDisabled();
    if (applied) {
        path = buf;
        ctx.markSceneDirty();
    }
    return applied;
}

// Remove Component（组件级通用；移除后调用方不得再使用 comp）
template <typename T>
inline void drawUIRemoveButton(EditorContext& ctx, NovaEngine::Component* comp, bool editMode) {
    if (!editMode) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component")) {
        comp->gameObject()->removeComponent<T>();
        ctx.markSceneDirty();
    }
    if (!editMode) ImGui::EndDisabled();
}

inline void drawCanvasEditor(EditorContext& ctx, NovaEngine::Canvas* c, bool editMode) {
    (void)editMode;
    ImGui::TextDisabled("(screen-space UI root, Transform ignored)");
    if (ImGui::DragFloat("UI Scale", &c->uiScale, 0.01f, 0.01f, 100.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
}

inline void drawImageEditor(EditorContext& ctx, NovaEngine::Image* img, bool editMode) {
    drawUIPathRow(ctx, "Sprite Path", img->spritePath, editMode);
    if (img->spritePath.empty()) ImGui::TextDisabled("(empty = solid color)");
    if (ImGui::ColorEdit4("Color", glm::value_ptr(img->color))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat4("9-Slice (L/T/R/B)", glm::value_ptr(img->slice9), 0.5f, 0.0f, 512.0f))
        ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::SliderFloat2("Anchor", glm::value_ptr(img->anchor), 0.0f, 1.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Offset", glm::value_ptr(img->offset), 0.5f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Size", glm::value_ptr(img->size), 0.5f, 0.0f, 10000.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
}

inline void drawTextEditor(EditorContext& ctx, NovaEngine::Text* t, bool editMode) {
    if (ImGui::InputText("Text", &t->text)) ctx.markDirtyOnly();  // 输入中间帧只置脏
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat("Font Size", &t->fontSize, 0.5f, 1.0f, 512.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::ColorEdit4("Color", glm::value_ptr(t->color))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    drawUIPathRow(ctx, "Font Path", t->fontPath, editMode);
    if (t->fontPath.empty()) ImGui::TextDisabled("(empty = default font msyh.ttc)");
    if (ImGui::SliderFloat2("Anchor", glm::value_ptr(t->anchor), 0.0f, 1.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Offset", glm::value_ptr(t->offset), 0.5f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
}

inline void drawButtonEditor(EditorContext& ctx, NovaEngine::Button* b, bool editMode) {
    (void)editMode;
    if (ImGui::ColorEdit4("Color", glm::value_ptr(b->color))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::ColorEdit4("Hover Color", glm::value_ptr(b->hoverColor))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::ColorEdit4("Pressed Color", glm::value_ptr(b->pressedColor))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat4("9-Slice (L/T/R/B)", glm::value_ptr(b->slice9), 0.5f, 0.0f, 512.0f))
        ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::SliderFloat2("Anchor", glm::value_ptr(b->anchor), 0.0f, 1.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Offset", glm::value_ptr(b->offset), 0.5f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Size", glm::value_ptr(b->size), 0.5f, 0.0f, 10000.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    ImGui::TextDisabled("onClick （脚本赋值）");  // C++ 内嵌脚本无 Inspector 事件绑定
}

// InspectorPanel 逐组件调用：dynamic_cast 分发 UI 四组件；非 UI 组件直接 return
inline void drawUIComponents(EditorContext& ctx, NovaEngine::Component* comp, bool editMode) {
    if (!comp) return;

    if (auto* c = dynamic_cast<NovaEngine::Canvas*>(comp)) {
        ImGui::PushID(comp);
        if (ImGui::CollapsingHeader("Canvas", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawCanvasEditor(ctx, c, editMode);
            drawUIRemoveButton<NovaEngine::Canvas>(ctx, comp, editMode);
        }
        ImGui::PopID();
        return;
    }
    if (auto* img = dynamic_cast<NovaEngine::Image*>(comp)) {
        ImGui::PushID(comp);
        if (ImGui::CollapsingHeader("Image", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawImageEditor(ctx, img, editMode);
            drawUIRemoveButton<NovaEngine::Image>(ctx, comp, editMode);
        }
        ImGui::PopID();
        return;
    }
    if (auto* t = dynamic_cast<NovaEngine::Text*>(comp)) {
        ImGui::PushID(comp);
        if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawTextEditor(ctx, t, editMode);
            drawUIRemoveButton<NovaEngine::Text>(ctx, comp, editMode);
        }
        ImGui::PopID();
        return;
    }
    if (auto* b = dynamic_cast<NovaEngine::Button*>(comp)) {
        ImGui::PushID(comp);
        if (ImGui::CollapsingHeader("Button", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawButtonEditor(ctx, b, editMode);
            drawUIRemoveButton<NovaEngine::Button>(ctx, comp, editMode);
        }
        ImGui::PopID();
    }
    // 其余组件（Transform/MeshRenderer/脚本等）不匹配：直接 return
}

}  // namespace NovaEditor
