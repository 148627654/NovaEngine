#pragma once
// ParticleWidgets.h —— 粒子系统 Inspector 字段控件（V0.5-6，c4 独占；header-only）。
// 波 2 合波接线（2026-11-19）：drawParticleComponents 与 drawPhysicsComponents/
// drawAudioComponents/drawUIComponents 同签名约定，由 InspectorPanel 组件循环分发
//（dynamic_cast 识别，不匹配 no-op）；字段控件 drawFields 复用同一漏斗。
//
// Edit 态点 play/stop/clear 安全：渲染器在未模拟（Edit）时始终画稳态快照，
// play() 只影响进入 Play 后的发射状态（onStart 会按 playOnAwake 再决定），
// 因此本控件无需感知编辑器 Play 状态即可接线。
#include "../EditorContext.h"
#include <imgui.h>
#include <NovaEngine/Particles/ParticleSystem.h>

namespace NovaEditor {
namespace ParticleWidgets {

// 粒子系统全部字段控件（数值范围按 §25.4 缺省值尺度给粗界；billboard v1 固定 true 禁用编辑）。
// 脏标记漏斗与既有面板一致：编辑中 markDirtyOnly（live 预览），编辑结束 markSceneDirty（入撤销栈）。
//（Edit 稳态快照每帧按参数重建 → 参数变更当帧在 SceneView 生效）
inline void drawFields(NovaEngine::ParticleSystem& ps, EditorContext& ctx) {
    if (ImGui::DragFloat("发射速率", &ps.emissionRate, 0.5f, 0.0f, 10000.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("寿命", &ps.lifetime, 0.05f, 0.01f, 60.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("初速", &ps.initialSpeed, 0.05f, 0.0f, 100.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("重力", &ps.gravity, 0.1f, -50.0f, 50.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::ColorEdit4("起始色", &ps.startColor.x)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::ColorEdit4("结束色", &ps.endColor.x)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("起始尺寸", &ps.startSize, 0.01f, 0.001f, 10.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragFloat("结束尺寸", &ps.endSize, 0.01f, 0.001f, 10.0f)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::InputText("贴图路径", &ps.texturePath)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragInt("tileX", &ps.tileX, 0.1f, 1, 16)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragInt("tileY", &ps.tileY, 0.1f, 1, 16)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::DragInt("粒子上限", &ps.maxParticles, 1.0f, 1, 100000)) ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
    if (ImGui::Checkbox("循环", &ps.loop)) ctx.markSceneDirty();
    if (ImGui::Checkbox("Play On Awake", &ps.playOnAwake)) ctx.markSceneDirty();
    ImGui::BeginDisabled();   // v1 固定 true（相机面向 quad）
    ImGui::Checkbox("Billboard", &ps.billboard);
    ImGui::EndDisabled();
    if (ImGui::Checkbox("加性混合", &ps.blendAdditive)) ctx.markSceneDirty();
    ImGui::Separator();
    ImGui::Text("存活粒子: %u | %s", ps.aliveCount(), ps.isPlaying() ? "发射中" : "已停止");
    // 运行期控制只影响 Play 后的发射状态（Edit 稳态快照不受影响），无需脏标记
    if (ImGui::Button("Play")) ps.play();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) ps.stop();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) ps.clear();
}

// 面板分发入口（Play 态约定与其它 widget 头一致：数值 live edit 保留；Remove 禁用）
inline void drawParticleComponents(EditorContext& ctx, NovaEngine::Component* comp, bool editMode) {
    auto* ps = dynamic_cast<NovaEngine::ParticleSystem*>(comp);
    if (!ps) return;
    if (!ImGui::CollapsingHeader("Particle System", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("psys");
    drawFields(*ps, ctx);
    if (!editMode) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Component"))
        comp->gameObject()->removeComponent<NovaEngine::ParticleSystem>();
    if (!editMode) ImGui::EndDisabled();
    ImGui::PopID();
}

}  // namespace ParticleWidgets
}  // namespace NovaEditor
