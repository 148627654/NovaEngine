// ParticleRenderer.cpp —— 骨架日桩（V0.5-6）：接口冻结于 §25.4；实例化渲染实现归 c4（独占）。
// 骨架日钩子空实现 = 零影响（SceneRenderer 遍历后调用，无粒子绘制）。
#include <NovaEngine/Particles/ParticleRenderer.h>

namespace NovaEngine {

void ParticleRenderer::init() {}
void ParticleRenderer::shutdown() {}

void ParticleRenderer::collectAndSubmit(const Scene& scene, const RenderCamera& camera) {
    (void)scene;
    (void)camera;
}

}  // namespace NovaEngine
