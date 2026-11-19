#pragma once
#include "../Panel.h"

#include <NovaEngine/Terrain/Terrain.h>   // Terrain::BrushMode

namespace NovaEditor {

// 地形笔刷共享设置（V0.5-8，c6）：SceneViewPanel 拖拽刷与 TerrainPanel 编辑共用
// 同一份状态（TerrainPanel dock 接线归合波日 c0——面板未实例化时 SceneView 用默认值，
// 刷子照常可用；结构体为进程内单例，不随场景序列化）。
struct TerrainBrushSettings {
    NovaEngine::Terrain::BrushMode mode = NovaEngine::Terrain::BrushMode::Raise;
    float radius = 4.0f;      // 笔刷半径（米）
    float strength = 0.5f;    // 强度 0..1
    static TerrainBrushSettings& instance();
};

// 地形面板（V0.5-8，c6）：dock 底部。笔刷模式下拉（抬升/压低/平整/平滑）、半径/强度
// 滑条、4 层材质编辑（albedo/normal 路径 + tileSize）、Save Heightmap 按钮。
// 目标地形 = 选中 GO 上的 Terrain，否则场景第一个 Terrain；无地形时显示提示。
class TerrainPanel : public Panel {
public:
    explicit TerrainPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    // 目标地形解析（选中优先 → 场景第一个；均无返回 nullptr）
    NovaEngine::Terrain* resolveTarget(EditorContext& ctx);
    void drawBrushSection(NovaEngine::Terrain* terrain, EditorContext& ctx);
    void drawLayersSection(NovaEngine::Terrain* terrain, EditorContext& ctx);
};

}  // namespace NovaEditor
