// TerrainRenderer.cpp —— 骨架日桩（V0.5-8）：接口冻结于 §25.6；GPU 网格/地形 shader 实现归 c6（独占）。
// 骨架日钩子空实现 = 零影响（SceneRenderer 遍历后调用，无地形绘制/阴影项）。
#include <NovaEngine/Terrain/TerrainRenderer.h>

namespace NovaEngine {

void TerrainRenderer::init() {}
void TerrainRenderer::shutdown() {}

void TerrainRenderer::collectAndSubmit(const Scene& scene, const RenderCamera& camera,
                                       const Frustum& frustum, std::vector<ShadowItem>& shadowItems) {
    (void)scene; (void)camera; (void)frustum; (void)shadowItems;
}

}  // namespace NovaEngine
