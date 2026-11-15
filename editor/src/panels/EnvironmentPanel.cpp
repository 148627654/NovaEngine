#include "EnvironmentPanel.h"

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装（ImGui 1.90+ 移除内建重载）

#include <NovaEngine/Render/SkyboxRenderer.h>
#include <NovaEngine/Scene/Scene.h>

#include <glm/gtc/type_ptr.hpp>

namespace NovaEditor {

EnvironmentPanel::EnvironmentPanel() : Panel("Environment") {}

void EnvironmentPanel::onGui(EditorContext& ctx, EditorCamera&) {
    using namespace NovaEngine;
    ImGui::Begin(m_name.c_str(), &open);
    Scene* scene = ctx.scene;
    if (!scene) {
        ImGui::End();
        return;
    }

    // 基础环境（V0.2-5）：天空色 / 环境光 / 网格开关 / 雾色（网格雾色参数化）
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("env");
        if (ImGui::ColorEdit3("Sky Color", glm::value_ptr(scene->skyColor))) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::ColorEdit3("Ambient Color", glm::value_ptr(scene->ambientColor)))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::Checkbox("Show Grid", &scene->showGrid)) ctx.markSceneDirty();
        if (ImGui::ColorEdit3("Fog Color", glm::value_ptr(scene->fogColor))) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        // 雾色一键回落到天空色（V0.2-5 联动语义）
        if (ImGui::Button("Reset Fog to Sky")) {
            scene->fogColor = scene->skyColor;
            ctx.markSceneDirty();
        }
        ImGui::PopID();
    }

    // 后处理（V0.3-14 波 3）：enabled/bloomThreshold/bloomIntensity/bloomLevels/exposure
    // → scene->postProcess（序列化 environment.postProcess 段）；脏标记漏斗与既有约定一致
    if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("pp");
        NovaEngine::PostProcessSettings& pp = scene->postProcess;
        if (ImGui::Checkbox("Enabled", &pp.enabled)) ctx.markSceneDirty();
        if (ImGui::DragFloat("Bloom Threshold", &pp.bloomThreshold, 0.01f, 0.0f, 10.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("Bloom Intensity", &pp.bloomIntensity, 0.01f, 0.0f, 5.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        // P2-2 修复：上限 3 与引擎钳制一致（PostProcess 内部 clamp 1-3）
        if (ImGui::DragInt("Bloom Levels", &pp.bloomLevels, 1.0f, 1, 3)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("Exposure", &pp.exposure, 0.01f, 0.01f, 10.0f)) ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        // 8bit 降级提示（§14.10：老集显无 RGB16F → 后处理运行期失效，不崩溃）
        if (!NovaEngine::PostProcess::hdrSupported())
            ImGui::TextDisabled("(HDR not supported on this GPU: post-processing disabled at runtime)");
        ImGui::PopID();
    }

    // 天空全组（V0.2-7）：模式 / 渐变两色 / cubemap 路径 / 旋转 / 曝光
    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("sky");
        int mode = static_cast<int>(scene->skyMode);
        const char* modes[] = {"Gradient", "Cubemap"};
        const int modeCount = static_cast<int>(sizeof(modes) / sizeof(modes[0]));
        if (ImGui::Combo("Sky Mode", &mode, modes, modeCount)) {
            scene->skyMode = static_cast<SkyMode>(mode);
            ctx.markSceneDirty();
        }

        if (ImGui::ColorEdit3("Horizon Color", glm::value_ptr(scene->skyHorizonColor)))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::ColorEdit3("Zenith Color", glm::value_ptr(scene->skyZenithColor)))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();

        // cubemap 路径（Enter 生效；空 = 渐变回退）
        std::string path = scene->skyCubemapPath;
        if (ImGui::InputText("Cubemap Path", &path, ImGuiInputTextFlags_EnterReturnsTrue)) {
            scene->skyCubemapPath = path;
            ctx.markSceneDirty();
        }
        ImGui::TextDisabled("(6-face dir or equirect file; empty = gradient fallback)");

        if (ImGui::DragFloat("Sky Rotation", &scene->skyRotation, 1.0f, -360.0f, 360.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        if (ImGui::DragFloat("Sky Exposure", &scene->skyExposure, 0.05f, 0.0f, 10.0f))
            ctx.markDirtyOnly();
        if (ImGui::IsItemDeactivatedAfterEdit()) ctx.markSceneDirty();
        ImGui::PopID();
    }

    ImGui::End();
}

}  // namespace NovaEditor
