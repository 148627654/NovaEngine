// TerrainPanel.cpp —— 地形面板（V0.5-8，c6，§25.6）
//
// 笔刷设置（模式/半径/强度）存于 TerrainBrushSettings 进程单例——SceneViewPanel 拖拽刷
// 与本面板共用（本面板 dock 接线归合波日 c0；接线前 SceneView 用默认设置刷子照常可用）。
// 层材质编辑直接写 Terrain 公共字段（序列化键 §25.6），脏标记走 ctx 漏斗；
// Save Heightmap 调 Terrain::saveHeightmap()（16bit PNG 写回）。

#include "TerrainPanel.h"

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>   // InputText(std::string*) 包装（ImGui 1.90+ 移除内建重载）

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/Scene.h>

namespace NovaEditor {

TerrainBrushSettings& TerrainBrushSettings::instance() {
    static TerrainBrushSettings s_instance;
    return s_instance;
}

TerrainPanel::TerrainPanel() : Panel("Terrain") {}

NovaEngine::Terrain* TerrainPanel::resolveTarget(EditorContext& ctx) {
    using namespace NovaEngine;
    if (ctx.scene == nullptr)
        return nullptr;
    // 选中优先（单选 → 同 GO → 子树内最近 Terrain）
    for (GameObject* go : ctx.selectedObjects()) {
        for (auto& c : go->getComponents<Component>())
            if (auto* t = dynamic_cast<Terrain*>(c))
                return t;
    }
    const auto terrains = ctx.scene->findComponents<Terrain>();
    return terrains.empty() ? nullptr : terrains.front();
}

void TerrainPanel::drawBrushSection(NovaEngine::Terrain* terrain, EditorContext& ctx) {
    using namespace NovaEngine;
    TerrainBrushSettings& brush = TerrainBrushSettings::instance();
    (void)terrain;

    // 笔刷模式下拉：抬升/压低/平整/平滑（§25.6 四模式）
    const char* modeNames[] = {"抬升 Raise", "压低 Lower", "平整 Flatten", "平滑 Smooth"};
    int mode = static_cast<int>(brush.mode);
    if (ImGui::Combo("Brush Mode", &mode, modeNames,
                     static_cast<int>(sizeof(modeNames) / sizeof(modeNames[0]))))
        brush.mode = static_cast<Terrain::BrushMode>(mode);
    if (ImGui::DragFloat("Radius", &brush.radius, 0.5f, 0.5f, 100.0f, "%.1f m")) {
        brush.radius = std::max(brush.radius, 0.5f);
        ctx.markDirtyOnly();   // 运行期设置不序列化：仅置脏不推快照
    }
    if (ImGui::SliderFloat("Strength", &brush.strength, 0.0f, 1.0f, "%.2f"))
        ctx.markDirtyOnly();

    // 保存高度图：写回 16bit PNG（路径为空时 saveHeightmap 内报错）
    if (ImGui::Button("Save Heightmap")) {
        if (terrain->saveHeightmap())
            ctx.markSceneDirty();
    }
    ImGui::TextDisabled("左键拖拽：场景视口工具栏开启 Terrain Brush 后在地形上刷");
}

void TerrainPanel::drawLayersSection(NovaEngine::Terrain* terrain, EditorContext& ctx) {
    for (int i = 0; i < 4; ++i) {
        NovaEngine::Terrain::TerrainLayer& layer = terrain->layers[i];
        if (ImGui::TreeNode((void*)(intptr_t)(i + 1), "Layer %d（splat %s）", i,
                            i == 0 ? "R" : (i == 1 ? "G" : (i == 2 ? "B" : "A")))) {
            bool changed = false;
            changed |= ImGui::InputText("Albedo", &layer.albedoPath);
            changed |= ImGui::InputText("Normal", &layer.normalPath);
            changed |= ImGui::DragFloat("Tile Size", &layer.tileSize, 0.5f, 0.5f, 200.0f,
                                        "%.1f m");
            if (changed)
                ctx.markSceneDirty();   // 层材质参与序列化：置脏 + Edit 态推快照
            ImGui::TreePop();
        }
    }
}

void TerrainPanel::onGui(EditorContext& ctx, EditorCamera& camera) {
    (void)camera;
    ImGui::Begin(m_name.c_str(), &open);

    NovaEngine::Terrain* terrain = resolveTarget(ctx);
    if (terrain == nullptr) {
        ImGui::TextDisabled("场景无 Terrain 组件（Hierarchy 中选中对象 Add Component → Terrain）");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("笔刷 Brush", ImGuiTreeNodeFlags_DefaultOpen))
        drawBrushSection(terrain, ctx);
    if (ImGui::CollapsingHeader("4 层材质 Layers", ImGuiTreeNodeFlags_DefaultOpen))
        drawLayersSection(terrain, ctx);

    ImGui::Separator();
    ImGui::TextDisabled("heightmap: %s（%u²，worldSize %.0f×%.0f m，heightScale %.1f m）",
                        terrain->heightmapPath.empty() ? "(平面)" : terrain->heightmapPath.c_str(),
                        terrain->resolution(), terrain->worldSize.x, terrain->worldSize.y,
                        terrain->heightScale);

    ImGui::End();
}

}  // namespace NovaEditor
