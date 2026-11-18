#pragma once
// ParticleWidgets.h —— 粒子系统 Inspector 字段控件（V0.5-6，c4 独占；header-only 草稿）。
// 合波日由 c0 接线进 InspectorPanel（本文件仅提供字段控件，不入 CMake 源清单——
// InspectorPanel include 本头后即编译生效）。
//
// Edit 态点 play/stop/clear 安全：渲染器在未模拟（Edit）时始终画稳态快照，
// play() 只影响进入 Play 后的发射状态（onStart 会按 playOnAwake 再决定），
// 因此本控件无需感知编辑器 Play 状态即可接线。
#include <imgui.h>
#include <NovaEngine/Particles/ParticleSystem.h>

namespace NovaEditor {
namespace ParticleWidgets {

// 粒子系统全部字段控件（合波日 InspectorPanel 的 ParticleSystem 分支调用）。
// 数值范围按 §25.4 缺省值尺度给粗界；billboard v1 固定 true（禁用编辑）。
inline void drawFields(NovaEngine::ParticleSystem& ps) {
    ImGui::DragFloat("发射速率", &ps.emissionRate, 0.5f, 0.0f, 10000.0f);
    ImGui::DragFloat("寿命", &ps.lifetime, 0.05f, 0.01f, 60.0f);
    ImGui::DragFloat("初速", &ps.initialSpeed, 0.05f, 0.0f, 100.0f);
    ImGui::DragFloat("重力", &ps.gravity, 0.1f, -50.0f, 50.0f);
    ImGui::ColorEdit4("起始色", &ps.startColor.x);
    ImGui::ColorEdit4("结束色", &ps.endColor.x);
    ImGui::DragFloat("起始尺寸", &ps.startSize, 0.01f, 0.001f, 10.0f);
    ImGui::DragFloat("结束尺寸", &ps.endSize, 0.01f, 0.001f, 10.0f);
    ImGui::InputText("贴图路径", &ps.texturePath);
    ImGui::DragInt("tileX", &ps.tileX, 0.1f, 1, 16);
    ImGui::DragInt("tileY", &ps.tileY, 0.1f, 1, 16);
    ImGui::DragInt("粒子上限", &ps.maxParticles, 1.0f, 1, 100000);
    ImGui::Checkbox("循环", &ps.loop);
    ImGui::Checkbox("Play On Awake", &ps.playOnAwake);
    ImGui::BeginDisabled();   // v1 固定 true（相机面向 quad）
    ImGui::Checkbox("Billboard", &ps.billboard);
    ImGui::EndDisabled();
    ImGui::Checkbox("加性混合", &ps.blendAdditive);
    ImGui::Separator();
    ImGui::Text("存活粒子: %u | %s", ps.aliveCount(), ps.isPlaying() ? "发射中" : "已停止");
    if (ImGui::Button("Play")) ps.play();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) ps.stop();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) ps.clear();
}

}  // namespace ParticleWidgets
}  // namespace NovaEditor
