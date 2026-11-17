#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <NovaEngine/Render/Renderer.h>

namespace NovaEngine {

class Scene;
class Mesh;
class Framebuffer;
struct RenderCamera;
struct PostProcessSettings;

// 场景渲染（V0.5-1 打包前置）：编辑器 Play 路径与 NovaPlayer 共用的单帧渲染
class SceneRenderer {
public:
    // 渲染一帧：调用方已绑定目标 FBO（视口已设）。内部：
    // 取主相机（isMain→第一个→回退 fallbackCamera）、第一个 DirectionalLight、
    // 天空、遍历收集（V0.5-14：视锥剔除 + 实例化分组，开关见 Renderer::setFrustumCullingEnabled）、
    // 阴影 pass（透明物体不进阴影）、网格地面（showGrid）、endFrame
    static void renderScene(const Scene& scene, const RenderCamera& fallbackCamera);

    // 取场景相机（isMain 优先，无则第一个 Camera，再无则复制 fallback）；Player 主循环取相机用
    static void collectCamera(const Scene& scene, const RenderCamera& fallback, RenderCamera& out);

    // ===== V0.3-14 后处理（波 3 冻结）：完整渲染链（Editor/Player 共用）=====
    // 描边条目（编辑器选中/悬停高亮；Play 下 outlines 传 nullptr）
    struct OutlineItem {
        std::shared_ptr<Mesh> mesh;
        glm::mat4 model = glm::mat4(1.0f);
        glm::vec3 color = glm::vec3(1.0f, 0.5f, 0.0f);
    };

    // 渲染场景到 hdrFbo → resolve → 后处理（或 RGBA8 直通）→ UIRenderer::render 到 outLdrFbo
    // - pp.enabled && PostProcess::hdrSupported()：hdrFbo 应为 RGBA16F（调用方在 resize 时决定格式）
    // - 否则：hdrFbo 以 RGBA8 创建 + blit 直通（与旧路径逐像素一致）
    // - outlines：编辑器描边（Play 下传 nullptr）
    // - afterSceneDraw：场景 FBO 内 3D pass 之后、resolve 之前执行的注入点（Show Colliders 线框等）
    // - useSceneCamera（V0.3-14 修复，波 4 后）：true=按场景相机选择（isMain→第一个→fallback，
    //   Play/Player 语义）；false=强制用 fallbackCamera（编辑器 Edit 语义——编辑器相机才是
    //   Scene View 的取景器，场景相机不得接管；否则场景里有 Camera 时 Edit 视图被其劫持）
    // - 返回 outLdrFbo 的颜色纹理 id（供 ImGui::Image / 交换缓冲使用）
    static uint32_t renderSceneToOutput(const Scene& scene, const RenderCamera& fallbackCamera,
                                        Framebuffer& hdrFbo, Framebuffer& outLdrFbo,
                                        const PostProcessSettings& pp,
                                        const std::vector<OutlineItem>* outlines = nullptr,
                                        const std::function<void()>& afterSceneDraw = {},
                                        bool useSceneCamera = true);
};

}  // namespace NovaEngine
