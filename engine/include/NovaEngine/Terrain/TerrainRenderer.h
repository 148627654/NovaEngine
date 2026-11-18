#pragma once
#include <vector>
#include <NovaEngine/Render/Frustum.h>
#include <NovaEngine/Render/Renderer.h>   // RenderCamera / ShadowItem

namespace NovaEngine {

class Scene;

class TerrainRenderer {
public:
    static void init(); static void shutdown();
    // 骨架日钩子（空实现默认）：懒建 GPU 网格（高度图→顶点/法线/索引 512²）→ 专用
    // terrain shader submit；castShadows 时地形进 shadowItems
    static void collectAndSubmit(const Scene& scene, const RenderCamera& camera,
                                 const Frustum& frustum, std::vector<ShadowItem>& shadowItems);
};

}  // namespace NovaEngine
