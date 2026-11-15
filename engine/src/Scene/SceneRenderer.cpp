// SceneRenderer.cpp —— 场景单帧渲染（V0.5-1 打包前置）
//
// 编辑器 Play 路径与 NovaPlayer 共用的渲染序列，与 SceneViewPanel 的 Play 分支同构：
//   取主相机（isMain → 第一个 → fallback）→ 第一个 DirectionalLight（无则默认光）
//   → 天空 → 单次遍历收集（activeInHierarchy + MeshRenderer；opaque 进阴影，transparent 不进）
//   → 阴影 pass（castShadows）→ 网格地面（showGrid，雾色取自 scene.fogColor）→ endFrame。
// 调用方负责：绑定目标 FBO、设好视口、以及 Renderer/SkyboxRenderer 的 init 生命周期。

#include <NovaEngine/Scene/SceneRenderer.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/MaterialAsset.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Cubemap.h>
#include <NovaEngine/Render/Framebuffer.h>
#include <NovaEngine/Render/PostProcess.h>
#include <NovaEngine/Render/ShadowMap.h>
#include <NovaEngine/Render/SkyboxRenderer.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/UI/UIRenderer.h>

#include <glm/geometric.hpp>              // dot（阴影盒中心投影）
#include <glm/gtc/matrix_transform.hpp>   // lookAt / perspective / radians / inverse

namespace NovaEngine {

namespace {

// 从 fallback 的投影矩阵反推宽高比（调用方 FBO 按目标分辨率设好；glm::perspective 下
// proj[1][1]/proj[0][0] = aspect），供场景相机的 projection 保持同一视口比例
float aspectOf(const RenderCamera& cam) {
    const float a = cam.projection[0][0];
    if (a == 0.0f) return 16.0f / 9.0f;
    return cam.projection[1][1] / a;
}

// 场景相机 → RenderCamera（与编辑器 Play 路径同一约定：forward = +Z，lookAt 朝向）
bool assembleFromCamera(const Camera* cam, const RenderCamera& fallback, RenderCamera& out) {
    const Transform* t = cam->transform();
    if (!t) return false;
    const glm::vec3 pos = t->worldPosition();
    out.view = glm::lookAt(pos, pos + t->forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    out.projection = glm::perspective(glm::radians(cam->fov), aspectOf(fallback),
                                      cam->nearPlane, cam->farPlane);
    out.position = pos;
    return true;
}

}  // namespace

// 场景 3D 渲染本体（renderScene 与 renderSceneToOutput 共用；不设置 gamma 模式——
// 由两个公开入口按路径设置，见各入口注释）
void renderSceneInternal(const Scene& scene, const RenderCamera& fallbackCamera,
                         bool useSceneCamera) {
    // 1. 渲染相机：useSceneCamera=true 走场景相机选择（isMain→第一个→fallback，Play 语义）；
    //    false 强制 fallback（编辑器 Edit 语义——场景相机不得劫持 Scene View 取景）
    RenderCamera renderCam;
    if (useSceneCamera) {
        SceneRenderer::collectCamera(scene, fallbackCamera, renderCam);
    } else {
        renderCam = fallbackCamera;
    }

    // 2. 灯光：第一个 DirectionalLight（方向取自 Transform.forward）；无则默认方向
    DirectionalLightData light;
    const DirectionalLight* dirLight = nullptr;
    scene.forEachObject([&](GameObject* go) {
        if (!dirLight)
            dirLight = go->getComponent<DirectionalLight>();
    });
    if (dirLight && dirLight->transform()) {
        light.direction = dirLight->transform()->forward();
        light.color = dirLight->color;
        light.intensity = dirLight->intensity;
    }
    light.ambient = scene.ambientColor;

    // 3. 阴影参数（castShadows 时组装；阴影盒中心 = 相机位置沿光轴投影到过原点平面，跟随相机）
    ShadowCameraParams shadowParams;
    std::vector<ShadowItem> shadowItems;
    const bool shadowEnabled = dirLight && dirLight->castShadows;
    if (shadowEnabled) {
        shadowParams.lightDirection = light.direction;
        shadowParams.mapSize = dirLight->shadowMapSize;
        shadowParams.bias = dirLight->shadowBias;
        const glm::vec3 camCenter =
            renderCam.position - light.direction * glm::dot(renderCam.position, light.direction);
        shadowParams.lightViewProj = computeLightViewProj(light.direction, camCenter,
                                                          dirLight->shadowRange,
                                                          dirLight->shadowMapSize);
    }

    // 4. 天空 cubemap（Cubemap 模式且路径非空时加载；失败/渐变走 nullptr（内部回退渐变））
    std::shared_ptr<Cubemap> skyCube;
    if (scene.skyMode == SkyMode::Cubemap && !scene.skyCubemapPath.empty())
        skyCube = AssetDatabase::instance().getCubemap(scene.skyCubemapPath);

    // 5. 渲染序列：天空 → 单次遍历收集 → 阴影 → grid → endFrame
    Renderer::beginFrame(renderCam, light, glm::vec4(scene.skyColor, 1.0f));
    SkyboxRenderer::draw(glm::inverse(renderCam.projection * renderCam.view), renderCam.position,
                         scene.skyMode, scene.skyHorizonColor, scene.skyZenithColor,
                         skyCube, scene.skyRotation, scene.skyExposure);
    scene.forEachObject([&](GameObject* go) {
        if (!go->activeInHierarchy()) return;
        const MeshRenderer* mr = go->getComponent<MeshRenderer>();
        if (!mr || !mr->mesh) return;
        const Transform* t = go->transform();
        if (!t) return;
        const glm::mat4 world = t->worldMatrix();
        // 材质双轨（V0.3-6，§14.5）：引用材质资产时整块取值（submit 与阴影收集共用同一解析结果）；
        // MaterialAsset 由资产线实现，此处只消费冻结头 Material.h 的 materialAsset 字段
        const Material& mat = mr->material.materialAsset ? mr->material.materialAsset->material()
                                                         : mr->material;
        Renderer::submit(mr->mesh, mat, world);
        // 透明物体不进阴影（裁定 2：队列归属只看 blendMode）
        if (shadowEnabled && mat.blendMode == BlendMode::Opaque)
            shadowItems.push_back(ShadowItem{mr->mesh, world});
    });
    if (shadowEnabled) Renderer::drawShadowPass(shadowParams, shadowItems);
    if (scene.showGrid) Renderer::drawGrid(scene.fogColor);
    Renderer::endFrame();
}

void SceneRenderer::renderScene(const Scene& scene, const RenderCamera& fallbackCamera) {
    // 旧路径（编辑器 Edit / 兼容调用）：固定非 HDR——uGamma=1/2.2，行为与波 2 逐像素一致
    // （本入口每帧重置 gamma 模式，防止与 renderSceneToOutput 的 HDR 帧交错时状态泄漏）
    Renderer::setGammaMode(false);
    renderSceneInternal(scene, fallbackCamera, /*useSceneCamera=*/true);
}

uint32_t SceneRenderer::renderSceneToOutput(const Scene& scene, const RenderCamera& fallbackCamera,
                                            Framebuffer& hdrFbo, Framebuffer& outLdrFbo,
                                            const PostProcessSettings& pp,
                                            const std::vector<OutlineItem>* outlines,
                                            const std::function<void()>& afterSceneDraw,
                                            bool useSceneCamera) {
    // 尺寸无效（最小化/未创建）：不渲染，返回 outLdrFbo 纹理（调用方跳过显示，不崩溃）
    if (hdrFbo.width() <= 0 || hdrFbo.height() <= 0 ||
        outLdrFbo.width() <= 0 || outLdrFbo.height() <= 0)
        return outLdrFbo.colorTextureId();

    // HDR 决策：pp.enabled && GL 支持可渲染 RGBA16F（hdrSupported 内含懒 init + 降级探测）。
    // HDR → 场景线性输出（uGamma=0 直通，combine 内 ACES+gamma）；否则 1/2.2（直通路径）
    const bool hdr = pp.enabled && PostProcess::hdrSupported();
    Renderer::setGammaMode(hdr);

    // 3D pass（含天空/阴影/网格）→ 描边（若有）→ afterSceneDraw 注入点（Show Colliders 线框等）→ resolve
    hdrFbo.bind();
    renderSceneInternal(scene, fallbackCamera, useSceneCamera);
    if (outlines != nullptr) {
        for (const OutlineItem& item : *outlines)
            if (item.mesh)
                Renderer::drawOutline(item.mesh, item.model, item.color);
    }
    if (afterSceneDraw)
        afterSceneDraw();
    hdrFbo.resolveMsaa();

    // 后处理（或 RGBA8 直通 blit）→ UI（outLdrFbo 内，§14.3 渲染顺序：HDR→post→UI→显示）
    PostProcess::resize(hdrFbo.width(), hdrFbo.height());
    outLdrFbo.bind();
    PostProcess::apply(hdrFbo.colorTextureId(), pp);
    UIRenderer::render(scene, outLdrFbo.width(), outLdrFbo.height());
    outLdrFbo.unbind();
    return outLdrFbo.colorTextureId();
}

void SceneRenderer::collectCamera(const Scene& scene, const RenderCamera& fallback,
                                  RenderCamera& out) {
    // isMain 优先（任意位置），无则第一个 Camera（深度优先序），再无则复制 fallback。
    // 跳过非激活对象（activeInHierarchy）：禁用的 isMain 相机不得参与 Play 渲染
    const Camera* first = nullptr;
    const Camera* main = nullptr;
    scene.forEachObject([&](GameObject* go) {
        if (!go->activeInHierarchy()) return;
        const Camera* cam = go->getComponent<Camera>();
        if (!cam) return;
        if (!first) first = cam;
        if (cam->isMain && !main) main = cam;
    });

    const Camera* chosen = main ? main : first;
    if (!chosen) {
        out = fallback;
        return;
    }
    if (!assembleFromCamera(chosen, fallback, out)) {
        // 相机组件无 Transform（畸形场景）：回退 fallback，不崩溃
        Logger::warn("[SceneRenderer] 场景相机 \"{}\" 缺少 Transform，回退 fallback 相机",
                     chosen->gameObject() ? chosen->gameObject()->name() : "?");
        out = fallback;
    }
}

}  // namespace NovaEngine
