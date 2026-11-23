// PhysicsLayersPanel.cpp —— 物理碰撞层面板（V1.0-3，§27.2）
//
// 16 层命名 + 16×16 对称矩阵（只展示三角区：行 a ≤ 列 b 的格；点击写 matrix[a][b] 与
// matrix[b][a] 两处，对称不变量由 UI 保证）+ 恢复默认（全通矩阵 + "Default"/"Layer1..15"）。
// 写 environment.physics 场景字段（Scene::physicsLayerNames / physicsCollisionMatrix =
// 权威存储，SceneSerializer 持久化）；Edit 态可改，Play 态整体禁用（进入 Play 时矩阵已
// 烘焙进 Jolt——改动不生效，禁用避免误导；与 PhysicsWidgets 同纪律）。

#include "PhysicsLayersPanel.h"

#include "../EditorContext.h"   // EditorContext 完整类型（Panel.h 仅前向声明）

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装

#include <NovaEngine/Physics/PhysicsWorld.h>   // kMaxLayers
#include <NovaEngine/Scene/Scene.h>

#include <string>

namespace NovaEditor {

namespace {
constexpr int kLayers = NovaEngine::PhysicsWorld::kMaxLayers;

// 展示名：层名非空用之，否则 "Layer N"（0 → "Default"）
std::string displayName(const NovaEngine::Scene& scene, int i) {
    if (size_t(i) < scene.physicsLayerNames.size() && !scene.physicsLayerNames[size_t(i)].empty())
        return scene.physicsLayerNames[size_t(i)];
    return i == 0 ? "Default" : "Layer " + std::to_string(i);
}
}  // namespace

PhysicsLayersPanel::PhysicsLayersPanel() : Panel("Physics Layers") {}

void PhysicsLayersPanel::onGui(EditorContext& ctx, EditorCamera&) {
    using namespace NovaEngine;

    ImGui::Begin(m_name.c_str(), &open);

    Scene* scene = ctx.scene;
    if (scene == nullptr) {
        ImGui::TextDisabled("无活动场景");
        ImGui::End();
        return;
    }

    const bool editMode = (ctx.state == EditorContext::EditorState::Edit);
    if (!editMode)
        ImGui::BeginDisabled();   // Play 态：矩阵已烘焙，改动不生效

    // ===== 层命名 =====
    ImGui::TextUnformatted("Layer Names");
    if (ImGui::BeginTable("##layerNames", 2, ImGuiTableFlags_SizingStretchProp)) {
        for (int i = 0; i < kLayers; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(std::to_string(i).c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i);
            std::string name = (size_t(i) < scene->physicsLayerNames.size())
                                   ? scene->physicsLayerNames[size_t(i)]
                                   : std::string();
            if (ImGui::InputText("##name", &name)) {
                while (scene->physicsLayerNames.size() < size_t(kLayers))
                    scene->physicsLayerNames.push_back(std::string());
                scene->physicsLayerNames[size_t(i)] = name;
                ctx.markSceneDirty();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    // ===== 碰撞矩阵（只展示三角区：行 a ≤ 列 b；点击镜像对称位）=====
    ImGui::TextUnformatted("Collision Matrix (symmetric; click mirrors)");
    ImGui::TextDisabled("行 = 层 a，列 = 层 b；只展示 a<=b 三角区");
    ImGui::BeginChild("##matrix", ImVec2(0, 360.0f), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##matrixTable", kLayers + 1,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        for (int b = 0; b < kLayers; ++b)
            ImGui::TableSetupColumn(std::to_string(b).c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    34.0f);
        ImGui::TableHeadersRow();
        for (int a = 0; a < kLayers; ++a) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(std::to_string(a).c_str());
            for (int b = 0; b < kLayers; ++b) {
                ImGui::TableSetColumnIndex(b + 1);
                if (a > b) {
                    ImGui::TextUnformatted("");   // 下三角镜像区不重复展示
                    continue;
                }
                ImGui::PushID(a * kLayers + b);
                bool enabled = scene->physicsCollisionMatrix[a][b];
                const std::string label =
                    "##cell" + std::to_string(a) + "_" + std::to_string(b);
                if (ImGui::Checkbox(label.c_str(), &enabled)) {
                    // 对称写（§27.2：矩阵对称；UI 只展示三角区）
                    scene->physicsCollisionMatrix[a][b] = enabled;
                    scene->physicsCollisionMatrix[b][a] = enabled;
                    ctx.markSceneDirty();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s x %s", displayName(*scene, a).c_str(),
                                      displayName(*scene, b).c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (ImGui::Button("恢复默认")) {
        for (int a = 0; a < kLayers; ++a)
            for (int b = 0; b < kLayers; ++b)
                scene->physicsCollisionMatrix[a][b] = true;   // 全通矩阵
        scene->physicsLayerNames.clear();
        for (int i = 0; i < kLayers; ++i)
            scene->physicsLayerNames.push_back(i == 0 ? "Default" : "Layer" + std::to_string(i));
        ctx.markSceneDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("写 environment.physics（随场景保存）");

    if (!editMode)
        ImGui::EndDisabled();

    ImGui::End();
}

}  // namespace NovaEditor
