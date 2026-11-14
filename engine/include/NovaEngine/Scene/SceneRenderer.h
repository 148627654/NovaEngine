#pragma once
#include <NovaEngine/Render/Renderer.h>

namespace NovaEngine {

class Scene;
struct RenderCamera;

// 场景渲染（V0.5-1 打包前置）：编辑器 Play 路径与 NovaPlayer 共用的单帧渲染
class SceneRenderer {
public:
    // 渲染一帧：调用方已绑定目标 FBO（视口已设）。内部：
    // 取主相机（isMain→第一个→回退 fallbackCamera）、第一个 DirectionalLight、
    // 天空、阴影 pass（透明物体不进阴影）、网格地面（showGrid）、
    // 遍历全部 MeshRenderer submit（opaque+transparent）、endFrame
    static void renderScene(const Scene& scene, const RenderCamera& fallbackCamera);

    // 取场景相机（isMain 优先，无则第一个 Camera，再无则复制 fallback）；Player 主循环取相机用
    static void collectCamera(const Scene& scene, const RenderCamera& fallback, RenderCamera& out);
};

}  // namespace NovaEngine
