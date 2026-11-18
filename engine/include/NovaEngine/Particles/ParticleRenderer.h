#pragma once
#include <NovaEngine/Render/Renderer.h>   // RenderCamera

namespace NovaEngine {

class Scene;

// 粒子渲染（V0.5-6）：遍历收集全部启用的 ParticleSystem → 合并实例数据一次 VBO 上传
// → 一次 glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count)。
// 透明段绘制、不写深度；绘制顺序 = 全部透明几何之后（粒子最后，保证可见）。
// 骨架日钩子：SceneRenderer 在 opaque 收集后调用（初始空实现由 c4 填充）。
class ParticleRenderer {
public:
    static void init(); static void shutdown();
    static void collectAndSubmit(const Scene& scene, const RenderCamera& camera);
};

}  // namespace NovaEngine
