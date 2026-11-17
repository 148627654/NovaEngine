#pragma once
// UIWidgets.h —— UI 组件 Inspector 编辑（V0.3-3 + V0.5-13 扩展）
//
// header-only inline（editor/CMakeLists 零改动）。InspectorPanel 逐组件调用
// drawUIComponents(ctx, comp, editMode)：dynamic_cast 分发 Canvas/Image/Text/Button/
// Slider/ScrollView 六组件，非 UI 组件直接 return（多组件同 GO 时每组件各自
// CollapsingHeader + PushID）。
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
#include <NovaEngine/UI/Slider.h>
#include <NovaEngine/UI/ScrollView.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>   // std::min / std::max（Slider 值域钳制）

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
    // ---- fill 三字段（V0.5-13 追加；缺省 None/1.0/true = 旧行为零变化）----
    static const char* kFillTypes[] = {"None", "Horizontal", "Vertical", "Radial90",
                                       "Radial180", "Radial360"};
    int fillType = static_cast<int>(img->fillType);
    if (ImGui::Combo("Fill Type", &fillType, kFillTypes,
                     static_cast<int>(sizeof(kFillTypes) / sizeof(kFillTypes[0])))) {
        img->fillType = static_cast<NovaEngine::ImageFillType>(fillType);
        ctx.markSceneDirty();  // 瞬时变更：即时入栈
    }
    if (img->fillType != NovaEngine::ImageFillType::None) {
        if (ImGui::SliderFloat("Fill Amount", &img->fillAmount, 0.0f, 1.0f)) ctx.markDirtyOnly();
        uiDirtyOnEditEnd(ctx);
        if (ImGui::Checkbox("Fill Clockwise", &img->fillClockwise)) ctx.markSceneDirty();
    }
}

inline void drawTextEditor(EditorContext& ctx, NovaEngine::Text* t, bool editMode) {
    // 2026-11-16 修复：PushID("value") 隔离字段 ID——CollapsingHeader("Text") 与
    // InputText("Text") 同栈同标签会算出同一 ImGui ID（header 不压栈），活动态被 header
    // 每帧清除（光标闪没）。压一层 ID 后两者互不干扰，可见标签不变。
    ImGui::PushID("value");
    if (ImGui::InputText("Text", &t->text)) ctx.markDirtyOnly();  // 输入中间帧只置脏
    ImGui::PopID();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat("Font Size", &t->fontSize, 0.5f, 1.0f, 512.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::ColorEdit4("Color", glm::value_ptr(t->color))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    drawUIPathRow(ctx, "Font Path", t->fontPath, editMode);
    if (t->fontPath.empty())
        ImGui::TextDisabled("(empty = Noto Sans SC / 等线 / 微软雅黑；可填 C:/Windows/Fonts/times.ttf 等)");
    if (ImGui::SliderFloat2("Anchor", glm::value_ptr(t->anchor), 0.0f, 1.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Offset", glm::value_ptr(t->offset), 0.5f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    // ---- wrap/wrapWidth/hAlign（V0.5-13 追加；缺省 false/0/Left = 旧渲染逐像素一致）----
    if (ImGui::Checkbox("Wrap", &t->wrap)) ctx.markSceneDirty();  // 瞬时变更：即时入栈
    if (t->wrap) {
        // wrapWidth <= 0 语义 = 取当前 Canvas 屏幕宽（引擎侧约定）
        if (ImGui::DragFloat("Wrap Width", &t->wrapWidth, 1.0f, 0.0f, 10000.0f)) ctx.markDirtyOnly();
        uiDirtyOnEditEnd(ctx);
        ImGui::TextDisabled("(wrapWidth <= 0 = Canvas screen width)");
    }
    static const char* kAligns[] = {"Left", "Center", "Right"};
    int align = static_cast<int>(t->hAlign);
    if (ImGui::Combo("H Align", &align, kAligns,
                     static_cast<int>(sizeof(kAligns) / sizeof(kAligns[0])))) {
        t->hAlign = static_cast<NovaEngine::TextHAlign>(align);
        ctx.markSceneDirty();
    }
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

// 滑条（V0.5-13）：value/min/max/wholeNumbers/三色/thickness/handleSize/布局/三 sprite 路径。
// value 编辑钳制在 [min,max]；路径行 Play 态禁用（与既有 UI 组件同纪律）。
inline void drawSliderEditor(EditorContext& ctx, NovaEngine::Slider* s, bool editMode) {
    drawUIPathRow(ctx, "Background Sprite", s->backgroundSpritePath, editMode);
    drawUIPathRow(ctx, "Fill Sprite", s->fillSpritePath, editMode);
    drawUIPathRow(ctx, "Handle Sprite", s->handleSpritePath, editMode);
    if (ImGui::ColorEdit4("Background Color", glm::value_ptr(s->backgroundColor))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::ColorEdit4("Fill Color", glm::value_ptr(s->fillColor))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::ColorEdit4("Handle Color", glm::value_ptr(s->handleColor))) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat("Fill Thickness", &s->fillThickness, 0.5f, 0.0f, 1000.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Handle Size", glm::value_ptr(s->handleSize), 0.5f, 0.0f, 10000.0f))
        ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    // 值域三件套：value 编辑时按当前 [min,max] 钳制（min>max 时交换防 ImGui 非法区间）
    if (ImGui::SliderFloat("Value", &s->value, std::min(s->minValue, s->maxValue),
                           std::max(s->minValue, s->maxValue)))
        ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat("Min", &s->minValue, 0.01f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat("Max", &s->maxValue, 0.01f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::Checkbox("Whole Numbers", &s->wholeNumbers)) ctx.markSceneDirty();
    if (ImGui::SliderFloat2("Anchor", glm::value_ptr(s->anchor), 0.0f, 1.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Offset", glm::value_ptr(s->offset), 0.5f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Size", glm::value_ptr(s->size), 0.5f, 0.0f, 10000.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
}

// 滚动容器（V0.5-13）：size/anchor/offset/双开关/sensitivity。
// scrollOffset 为运行期内容布局产物（位置记忆随序列化），Inspector 不直接编辑。
inline void drawScrollViewEditor(EditorContext& ctx, NovaEngine::ScrollView* sv, bool editMode) {
    (void)editMode;
    if (ImGui::SliderFloat2("Anchor", glm::value_ptr(sv->anchor), 0.0f, 1.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Offset", glm::value_ptr(sv->offset), 0.5f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::DragFloat2("Size", glm::value_ptr(sv->size), 0.5f, 0.0f, 10000.0f)) ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
    if (ImGui::Checkbox("Scroll Horizontal", &sv->scrollHorizontal)) ctx.markSceneDirty();
    if (ImGui::Checkbox("Scroll Vertical", &sv->scrollVertical)) ctx.markSceneDirty();
    if (ImGui::DragFloat("Scroll Sensitivity", &sv->scrollSensitivity, 0.5f, 0.1f, 1000.0f))
        ctx.markDirtyOnly();
    uiDirtyOnEditEnd(ctx);
}

// InspectorPanel 逐组件调用：dynamic_cast 分发 UI 六组件；非 UI 组件直接 return
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
        return;
    }
    if (auto* s = dynamic_cast<NovaEngine::Slider*>(comp)) {
        ImGui::PushID(comp);
        if (ImGui::CollapsingHeader("Slider", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawSliderEditor(ctx, s, editMode);
            drawUIRemoveButton<NovaEngine::Slider>(ctx, comp, editMode);
        }
        ImGui::PopID();
        return;
    }
    if (auto* sv = dynamic_cast<NovaEngine::ScrollView*>(comp)) {
        ImGui::PushID(comp);
        if (ImGui::CollapsingHeader("Scroll View", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawScrollViewEditor(ctx, sv, editMode);
            drawUIRemoveButton<NovaEngine::ScrollView>(ctx, comp, editMode);
        }
        ImGui::PopID();
    }
    // 其余组件（Transform/MeshRenderer/脚本等）不匹配：直接 return
}

}  // namespace NovaEditor
