#pragma once
// MaterialEditorWidgets.h —— 材质编辑控件集（V0.3-6）
//
// Inspector（单对象/多选）与 AssetBrowser 的 .novamat 编辑模态共用同一套控件，
// 保证两处行为一致（键名/标签/脏标记漏斗/贴图 sRGB 约定）。
// 复用函数放头文件（inline，非 .cpp）→ editor/CMakeLists 无需改动。
//
// 脏标记漏斗（V0.2-2）：拖拽/输入中 markDirtyOnly；编辑结束（IsItemDeactivatedAfterEdit）
// 或瞬时控件（Combo/Checkbox/Enter）markSceneDirty。
// markDirty=false（资产编辑模态）：纯控件不置脏——仅 Save 时统一落盘 + reload + markSceneDirty。
// Play 态约定：数值/颜色 live edit 保留；贴图路径输入禁用（与既有 Inspector 行为一致）。

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Material.h>

#include <glm/gtc/type_ptr.hpp>

#include <string>

namespace NovaEditor {

// PBR 贴图槽描述：成员指针免 per-slot 复制（单对象/多选/资产模态共用）
struct MaterialTextureSlot {
    const char* label;                                          // "Albedo Map" 等
    bool srgb;                                                  // albedo=true；normal/metallic/roughness/emission=false
    std::string NovaEngine::Material::*pathField;               // 序列化路径字符串
    std::shared_ptr<NovaEngine::Texture> NovaEngine::Material::*mapField;   // 运行时 GPU 纹理
};

inline const MaterialTextureSlot kMaterialTextureSlots[] = {
    {"Albedo Map", true, &NovaEngine::Material::albedoMapPath, &NovaEngine::Material::albedoMap},
    {"Normal Map", false, &NovaEngine::Material::normalMapPath, &NovaEngine::Material::normalMap},
    {"Metallic Map", false, &NovaEngine::Material::metallicMapPath, &NovaEngine::Material::metallicMap},
    {"Roughness Map", false, &NovaEngine::Material::roughnessMapPath, &NovaEngine::Material::roughnessMap},
    {"Emission Map", false, &NovaEngine::Material::emissionMapPath, &NovaEngine::Material::emissionMap},
};

// 单贴图路径行：显示当前路径；Enter 应用（空 = 清除；加载失败 warn 保持原状）。
// EnterReturnsTrue 只在 Enter 分支应用值 → 只在 Enter 分支置脏（点击别处不应用也不置脏）。
inline void drawMaterialTexturePath(NovaEngine::Material& material, const MaterialTextureSlot& slot,
                                    EditorContext& ctx, bool markDirty) {
    using namespace NovaEngine;
    const bool edit = ctx.state == EditorContext::EditorState::Edit;

    const std::string boundLabel =
        (material.*slot.pathField).empty()
            ? std::string(slot.label) + ": none"
            : std::string(slot.label) + ": " + (material.*slot.pathField);
    ImGui::TextUnformatted(boundLabel.c_str());
    std::string pathBuf = material.*slot.pathField;
    if (!edit) ImGui::BeginDisabled();
    const std::string inputLabel = std::string(slot.label) + " Path";
    if (ImGui::InputText(inputLabel.c_str(), &pathBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (pathBuf.empty()) {
            material.*slot.mapField = nullptr;
            material.*slot.pathField = std::string();
            if (markDirty) ctx.markSceneDirty();
        } else {
            auto tex = AssetDatabase::instance().getTexture(pathBuf, slot.srgb);
            if (tex) {
                material.*slot.mapField = tex;
                material.*slot.pathField = pathBuf;   // 同步写路径（序列化用，避免 Ctrl+S 丢失）
                if (markDirty) ctx.markSceneDirty();
            } else {
                Logger::warn("贴图加载失败: {}（{} 保持原状）", pathBuf, slot.label);
            }
        }
    }
    if (!edit) ImGui::EndDisabled();
}

// 完整材质编辑控件集：数值/颜色 live edit、贴图路径 Enter 生效（Play 态路径输入禁用）。
// 双轨（Material Asset Assign/Detach）不在此处——那是 MeshRenderer 级别职责（Inspector 独有）。
inline void drawMaterialEditor(NovaEngine::Material& material, EditorContext& ctx,
                               bool markDirty = true) {
    using namespace NovaEngine;

    // 颜色/数值（live edit 在 Play 态保留）
    if (ImGui::ColorEdit3("Albedo", glm::value_ptr(material.albedo)) && markDirty)
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit() && markDirty) ctx.markSceneDirty();
    if (ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f) && markDirty)
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit() && markDirty) ctx.markSceneDirty();
    if (ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f) && markDirty)
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit() && markDirty) ctx.markSceneDirty();

    // 透明参数（V0.2-8）：混合系数 + 渲染队列模式
    if (ImGui::SliderFloat("Alpha", &material.alpha, 0.0f, 1.0f) && markDirty)
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit() && markDirty) ctx.markSceneDirty();
    int blendMode = static_cast<int>(material.blendMode);
    const char* blendModes[] = {"Opaque", "AlphaBlend"};
    if (ImGui::Combo("Blend Mode", &blendMode, blendModes,
                     static_cast<int>(sizeof(blendModes) / sizeof(blendModes[0])))) {
        material.blendMode = static_cast<BlendMode>(blendMode);
        if (markDirty) ctx.markSceneDirty();
    }

    // V0.3-4 PBR 发射参数
    if (ImGui::ColorEdit3("Emission Color", glm::value_ptr(material.emissionColor)) && markDirty)
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit() && markDirty) ctx.markSceneDirty();
    if (ImGui::DragFloat("Emission Intensity", &material.emissionIntensity, 0.05f, 0.0f, 100.0f) &&
        markDirty)
        ctx.markDirtyOnly();
    if (ImGui::IsItemDeactivatedAfterEdit() && markDirty) ctx.markSceneDirty();

    // PBR 贴图路径行（Enter 生效；albedo sRGB，法线/金属/粗糙/发射线性）
    for (const MaterialTextureSlot& slot : kMaterialTextureSlots)
        drawMaterialTexturePath(material, slot, ctx, markDirty);
}

}  // namespace NovaEditor
