#pragma once
// AudioWidgets.h —— Inspector 音频组件编辑控件（V0.3-2，header-only inline）
//
// 波 2 组件 widget 漏斗约定：InspectorPanel 对选中对象的全部组件依次调用
//   drawPhysicsComponents(ctx, comp, editMode);   // A：physics（并行创建）
//   drawAudioComponents(ctx, comp, editMode);     // C：audio（本头）
//   drawUIComponents(ctx, comp, editMode);        // B：UI（并行创建）
// 各头对自身组件类型 dynamic_cast 分发，不匹配类型 no-op（无任何 ImGui 输出）。
//
// 脏标记漏斗（与 InspectorPanel 既有约定一致）：
//   拖拽/输入中 markDirtyOnly；编辑结束（IsItemDeactivatedAfterEdit）markSceneDirty；
//   Checkbox 无中间帧 → 变更即 markSceneDirty。
//
// 注意：Remove 按钮会真正释放组件（本函数调用方负责在返回后校验组件存活）。

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装

#include <NovaEngine/Scene/AudioListener.h>
#include <NovaEngine/Scene/AudioSource.h>

#include "../EditorContext.h"

namespace NovaEditor {

inline void drawAudioComponents(EditorContext& ctx, NovaEngine::Component* comp, bool editMode) {
    using namespace NovaEngine;
    if (comp == nullptr)
        return;

    if (auto* src = dynamic_cast<AudioSource*>(comp)) {
        // ===== 音源（§14.2）：clipPath/volume/loop/spatialBlend/minDistance/maxDistance/playOnAwake =====
        if (!ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        ImGui::PushID("asrc");
        bool remove = false;

        // 资产路径（Enter 生效；Play 态禁用输入）
        std::string path = src->clipPath;
        if (!editMode) ImGui::BeginDisabled();
        if (ImGui::InputText("Clip", &path, ImGuiInputTextFlags_EnterReturnsTrue)) {
            src->clipPath = path;
            ctx.markSceneDirty();
        }
        if (!editMode) ImGui::EndDisabled();
        ImGui::TextDisabled("(assets/... .wav/.mp3/.flac/.ogg path, Enter)");

        if (ImGui::SliderFloat("Volume", &src->volume, 0.0f, 1.0f)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::Checkbox("Loop", &src->loop)) ctx.markSceneDirty();
        if (ImGui::SliderFloat("Spatial Blend", &src->spatialBlend, 0.0f, 1.0f)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        ImGui::TextDisabled("(0 = 2D full volume, 1 = 3D distance attenuation)");
        if (ImGui::DragFloat("Min Distance", &src->minDistance, 0.1f, 0.0f, 1000.0f)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("Max Distance", &src->maxDistance, 0.1f, 0.0f, 1000.0f)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::Checkbox("Play On Awake", &src->playOnAwake)) ctx.markSceneDirty();

        // 运行期控制（不序列化、不置脏）：仅 Play 态可用（Edit 态 onStart 未广播，听不到）
        if (editMode) ImGui::BeginDisabled();
        if (ImGui::Button("Play")) src->Play();
        ImGui::SameLine();
        if (ImGui::Button("Stop")) src->Stop();
        if (editMode) ImGui::EndDisabled();

        if (!editMode) ImGui::BeginDisabled();
        if (ImGui::Button("Remove Component")) remove = true;
        if (!editMode) ImGui::EndDisabled();
        ImGui::PopID();
        if (remove) {
            // AudioSource 析构自动 stop（引擎侧）；调用方需在返回后校验组件存活
            src->gameObject()->removeComponent<AudioSource>();
            ctx.markSceneDirty();
        }
    } else if (auto* listener = dynamic_cast<AudioListener*>(comp)) {
        // ===== 监听器（§14.2）：无字段，仅提示 =====
        if (!ImGui::CollapsingHeader("Audio Listener", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        ImGui::PushID("alist");
        bool remove = false;
        ImGui::TextDisabled("(no fields - attach to the main camera GameObject)");
        if (!editMode) ImGui::BeginDisabled();
        if (ImGui::Button("Remove Component")) remove = true;
        if (!editMode) ImGui::EndDisabled();
        ImGui::PopID();
        if (remove) {
            listener->gameObject()->removeComponent<AudioListener>();
            ctx.markSceneDirty();
        }
    }
    // 其它组件类型：no-op（漏斗约定）
}

}  // namespace NovaEditor
