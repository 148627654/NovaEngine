// SceneRenderer.cpp —— 场景单帧渲染（V0.5-1 打包前置）
//
// 编辑器 Play 路径与 NovaPlayer 共用的渲染序列，与 SceneViewPanel 的 Play 分支同构：
//   取主相机（isMain → 第一个 → fallback）→ 第一个 DirectionalLight（无则默认光）
//   → 天空 → 单次遍历收集（activeInHierarchy + MeshRenderer；V0.5-14 视锥剔除 + 实例化分组；
//     opaque 进阴影，transparent 不进）
//   → 阴影 pass（castShadows）→ 网格地面（showGrid，雾色取自 scene.fogColor）→ endFrame。
// 调用方负责：绑定目标 FBO、设好视口、以及 Renderer/SkyboxRenderer 的 init 生命周期。

#include <NovaEngine/Scene/SceneRenderer.h>

#include <NovaEngine/Animation/SkeletalMeshRenderer.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/MaterialAsset.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Particles/ParticleRenderer.h>
#include <NovaEngine/Render/Cubemap.h>
#include <NovaEngine/Render/Framebuffer.h>
#include <NovaEngine/Render/Frustum.h>
#include <NovaEngine/Render/IBL.h>
#include <NovaEngine/Render/PostProcess.h>
#include <NovaEngine/Render/SSAO.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/ShadowMap.h>
#include <NovaEngine/Render/SkyboxRenderer.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/LODGroup.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/SpriteRenderer.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/Terrain/TerrainRenderer.h>
#include <NovaEngine/UI/UIRenderer.h>

#include <glad/gl.h>

#include <algorithm>
#include <cstddef>

#include <glm/geometric.hpp>              // dot（阴影盒中心投影）
#include <glm/gtc/matrix_transform.hpp>   // lookAt / perspective / ortho / radians / inverse

namespace NovaEngine {

namespace {

// 局部 AABB → 世界 AABB（V0.5-14 剔除用）：8 角点经世界矩阵变换后取包围盒。
// 相比「中心 + 半径」方法，角点变换对旋转与非均匀缩放均给出保守精确的世界包围（无漏剔除）
AABB transformAabb(const AABB& local, const glm::mat4& world) {
    AABB out;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner(
            (i & 1) ? local.max.x : local.min.x,
            (i & 2) ? local.max.y : local.min.y,
            (i & 4) ? local.max.z : local.min.z);
        const glm::vec3 p = glm::vec3(world * glm::vec4(corner, 1.0f));
        out.min.x = std::min(out.min.x, p.x);
        out.min.y = std::min(out.min.y, p.y);
        out.min.z = std::min(out.min.z, p.z);
        out.max.x = std::max(out.max.x, p.x);
        out.max.y = std::max(out.max.y, p.y);
        out.max.z = std::max(out.max.z, p.z);
    }
    return out;
}

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
    // V0.5-10 2D（§25.8）：按投影模式分支——缺省 Perspective = 旧行为零变化；
    // 正交半高 = orthoSize（Unity 语义），半宽按 fallback 宽高比保持视口比例
    if (cam->projectionMode == ProjectionMode::Orthographic) {
        const float aspect = aspectOf(fallback);
        const float halfH = glm::max(cam->orthoSize, 0.001f);
        const float halfW = halfH * aspect;
        out.projection = glm::ortho(-halfW, halfW, -halfH, halfH, cam->nearPlane, cam->farPlane);
    } else {
        out.projection = glm::perspective(glm::radians(cam->fov), aspectOf(fallback),
                                          cam->nearPlane, cam->farPlane);
    }
    out.position = pos;
    return true;
}

// ===== 2D 精灵渲染（V0.5-10 骨架日最小版，c8 后续可改进 sprite.vert）=====
// sprite.vert（世界变换 quad）+ ui.frag（vColor × 贴图 alpha）复用；
// 收集 → (sortingLayer, orderInLayer, 视距) 排序 → 顺序提交（层越大越后绘，§25.8）。
std::shared_ptr<Shader> s_spriteShader;
uint32_t s_spriteVao = 0, s_spriteVbo = 0;
std::shared_ptr<Texture> s_spriteWhiteTex;   // 空贴图 = 白块（1×1 世界单位，Unity 默认精灵语义）

struct SpriteItem {
    glm::mat4 world;
    std::shared_ptr<Texture> texture;
    glm::vec4 color;
    int sortingLayer = 0, orderInLayer = 0;
    float viewDepth = 0.0f;   // 视空间深度（远 → 大）
    float sizeX = 1.0f, sizeY = 1.0f;
    bool flipX = false, flipY = false;
};

void ensureSpriteResources() {
    if (!s_spriteShader)
        s_spriteShader = Shader::createFromFiles("assets/shaders/sprite.vert",
                                                 "assets/shaders/ui.frag");
    if (s_spriteVao == 0) {
        // quad VAO + 动态 VBO（每精灵 6 顶点，交错 pos(3) + uv(2) + color(4)）
        glGenVertexArrays(1, &s_spriteVao);
        glBindVertexArray(s_spriteVao);
        glGenBuffers(1, &s_spriteVbo);
        glBindBuffer(GL_ARRAY_BUFFER, s_spriteVbo);
        constexpr GLsizei kStride = 9 * static_cast<GLsizei>(sizeof(float));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(5 * sizeof(float)));
        glBindVertexArray(0);
    }
    if (!s_spriteWhiteTex)
        s_spriteWhiteTex = Texture::createWhite();
}

void drawSprites(const Scene& scene, const RenderCamera& renderCam) {
    std::vector<SpriteItem> sprites;
    scene.forEachObject([&](GameObject* go) {
        if (!go->activeInHierarchy()) return;
        const SpriteRenderer* sr = go->getComponent<SpriteRenderer>();
        if (!sr) return;
        const Transform* t = go->transform();
        if (!t) return;
        SpriteItem item;
        item.world = t->worldMatrix();
        item.texture = sr->texture;
        item.color = sr->color;
        item.sortingLayer = sr->sortingLayer;
        item.orderInLayer = sr->orderInLayer;
        item.flipX = sr->flipX;
        item.flipY = sr->flipY;
        const float ppu = sr->pixelsPerUnit > 0.0f ? sr->pixelsPerUnit : 100.0f;
        if (sr->texture) {
            item.sizeX = static_cast<float>(sr->texture->width()) / ppu;
            item.sizeY = static_cast<float>(sr->texture->height()) / ppu;
        } else {
            item.sizeX = 100.0f / ppu;   // 白块 = 100×100 px 贴图（Unity 默认精灵）
            item.sizeY = item.sizeX;
        }
        item.viewDepth = -(renderCam.view * glm::vec4(t->worldPosition(), 1.0f)).z;
        sprites.push_back(std::move(item));
    });
    if (sprites.empty()) return;

    // 排序：层/序升序（大层后绘覆盖），同层按视距降序（远先画，画家算法）
    std::sort(sprites.begin(), sprites.end(), [](const SpriteItem& a, const SpriteItem& b) {
        if (a.sortingLayer != b.sortingLayer) return a.sortingLayer < b.sortingLayer;
        if (a.orderInLayer != b.orderInLayer) return a.orderInLayer < b.orderInLayer;
        return a.viewDepth > b.viewDepth;
    });

    ensureSpriteResources();
    if (!s_spriteShader) return;   // shader 失败：静默不绘制（不崩溃）

    // 单输出 pass（ui.frag 仅 fragColor）：切 A0，防 MRT FBO 下唯一输出广播污染
    // 法线/线性深度附件（V0.5-5 SSAO，c3 探针复现）
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    // 透明段：AlphaBlend + 不写深度（深度仍测试——被 3D 几何正确遮挡；排序序 = 层序）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    s_spriteShader->bind();
    s_spriteShader->setInt("uTex", 0);
    glBindVertexArray(s_spriteVao);
    for (const SpriteItem& item : sprites) {
        const float hx = item.sizeX * 0.5f, hy = item.sizeY * 0.5f;
        const float u0 = item.flipX ? 1.0f : 0.0f, u1 = item.flipX ? 0.0f : 1.0f;
        const float v0 = item.flipY ? 1.0f : 0.0f, v1 = item.flipY ? 0.0f : 1.0f;
        // 两个三角形（对象空间，X 右 Y 上——2D 平面 = 世界 XY）
        const float verts[6 * 9] = {
            -hx, -hy, 0.0f, u0, v0, item.color.r, item.color.g, item.color.b, item.color.a,
             hx, -hy, 0.0f, u1, v0, item.color.r, item.color.g, item.color.b, item.color.a,
             hx,  hy, 0.0f, u1, v1, item.color.r, item.color.g, item.color.b, item.color.a,
            -hx, -hy, 0.0f, u0, v0, item.color.r, item.color.g, item.color.b, item.color.a,
             hx,  hy, 0.0f, u1, v1, item.color.r, item.color.g, item.color.b, item.color.a,
            -hx,  hy, 0.0f, u0, v1, item.color.r, item.color.g, item.color.b, item.color.a,
        };
        s_spriteShader->setMat4("uModel", item.world);
        glBindBuffer(GL_ARRAY_BUFFER, s_spriteVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
        (item.texture ? item.texture : s_spriteWhiteTex)->bind(0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
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
    //    + IBL 环境（V0.5-4，§25.2 渲染接线）：天空盒与 IBL 同源（v1 无独立 IBL 源路径）。
    //    loadCached 失败（渐变天空/6 面目录源/烘焙失败）→ setEnvironment(0,0,0,0)
    //    （blinn_phong.frag 间接光贡献 0，程序化回退 = 既有 ambientColor，不黑屏）。
    std::shared_ptr<Cubemap> skyCube;
    if (scene.skyMode == SkyMode::Cubemap && !scene.skyCubemapPath.empty()) {
        skyCube = AssetDatabase::instance().getCubemap(scene.skyCubemapPath);
        const std::shared_ptr<IblEnvironment> env = IBL::loadCached(scene.skyCubemapPath);
        if (env)
            Renderer::setEnvironment(env->irradianceId(), env->prefilterId(), env->brdfLutId(),
                                     scene.iblIntensity);
        else
            Renderer::setEnvironment(0, 0, 0, 0);
    } else {
        Renderer::setEnvironment(0, 0, 0, 0);
    }
    // 天空旋转对齐（§25.2 合波补）：IBL 采样方向与天空盒同款 +Y 旋转（渐变天空无 IBL，设了无副作用）
    Renderer::setIblSkyRotation(glm::radians(scene.skyRotation));

    // 5. 渲染序列：天空 → 单次遍历收集（V0.5-14 视锥剔除 + 实例化分组）→ 阴影 → grid → endFrame
    Renderer::beginFrame(renderCam, light, glm::vec4(scene.skyColor, 1.0f));
    // 天空 = 单输出 pass（只写 location 0）：MRT FBO 下切 A0，防唯一输出广播写入法线/
    // 线性深度附件（V0.5-5 SSAO，c3 探针复现；SkyboxRenderer 自身不改 draw buffer 状态）
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    NE_GPU_SCOPE("sky");   // V1.0-1 GPU 计时段（§26.0 骨架日；Profiler 关闭 = 零开销）
    SkyboxRenderer::draw(glm::inverse(renderCam.projection * renderCam.view), renderCam.position,
                         scene.skyMode, scene.skyHorizonColor, scene.skyZenithColor,
                         skyCube, scene.skyRotation, scene.skyExposure);

    // 视锥（V0.5-14）：由渲染相机 view × projection 构造；开关关闭时仍构造（成本可忽略），
    // 仅跳过 intersects 测试——遍历行为与波 4 基线逐帧一致（回归 A/B）
    const Frustum frustum = Frustum::fromViewProj(renderCam.projection * renderCam.view);

    // 收集 (mesh, material, world) 三元组：剔除后的可见项（提交与阴影收集同源，保证一致）
    struct SceneItem {
        std::shared_ptr<Mesh> mesh;
        Material material;
        glm::mat4 world;
    };
    std::vector<SceneItem> items;
    scene.forEachObject([&](GameObject* go) {
        if (!go->activeInHierarchy()) return;
        const Transform* t = go->transform();
        if (!t) return;
        const glm::mat4 world = t->worldMatrix();
        // V0.5-9 蒙皮网格（波 2 2b 合波裁定 A，2026-11-19，c7 请求）：SMR 与 MeshRenderer
        // 同路径收集——走既有 submit/剔除/阴影/材质双轨；无 LOD 替换；每个 SMR 实例
        // 自建 GPU Mesh（指针唯一 → 实例化分组自然不命中）；骨骼矩阵由 Animator 每帧
        // 经 Mesh::setBoneMatrices 上传。提取失败的回退静态网格同样经此路径提交。
        if (const SkeletalMeshRenderer* smr = go->getComponent<SkeletalMeshRenderer>()) {
            if (smr->mesh) {
                if (Renderer::frustumCullingEnabled() &&
                    !frustum.intersects(transformAabb(smr->mesh->aabb(), world))) {
                    Renderer::markCulled();
                } else {
                    const Material& smat = smr->material.materialAsset
                                               ? smr->material.materialAsset->material()
                                               : smr->material;
                    if (shadowEnabled && smat.blendMode == BlendMode::Opaque)
                        shadowItems.push_back(ShadowItem{smr->mesh, world});
                    items.push_back(SceneItem{smr->mesh, smat, world});
                }
            }
        }
        const MeshRenderer* mr = go->getComponent<MeshRenderer>();
        if (!mr || !mr->mesh) return;
        // V0.5-3 LOD（§25.0 遍历扩展点）：mesh 来源替换——resolveMeshFor 无 LODGroup/无可用档
        // → nullptr 回退 mr->mesh（= 旧路径零变化）；剔除/材质双轨/实例化分组/阴影收集逻辑
        // 不变，全部使用同一替代结果
        std::shared_ptr<Mesh> mesh = LODGroup::resolveMeshFor(go, renderCam.position);
        if (!mesh) mesh = mr->mesh;
        // 世界 AABB：局部 AABB 8 角点经世界矩阵变换取包围盒（非均匀缩放/旋转精确）；
        // 锥体外 → 跳过 submit 与阴影收集（被剔除物体不写阴影贴图，无幽灵阴影）
        if (Renderer::frustumCullingEnabled() &&
            !frustum.intersects(transformAabb(mesh->aabb(), world))) {
            Renderer::markCulled();
            return;
        }
        // 材质双轨（V0.3-6，§14.5）：引用材质资产时整块取值（submit 与阴影收集共用同一解析结果）；
        // MaterialAsset 由资产线实现，此处只消费冻结头 Material.h 的 materialAsset 字段
        const Material& mat = mr->material.materialAsset ? mr->material.materialAsset->material()
                                                         : mr->material;
        // 透明物体不进阴影（裁定 2：队列归属只看 blendMode）
        if (shadowEnabled && mat.blendMode == BlendMode::Opaque)
            shadowItems.push_back(ShadowItem{mesh, world});
        items.push_back(SceneItem{mesh, mat, world});
    });

    // V0.5-14 实例化分组（冻结算法）：键 = (mesh 指针, material ==)；
    // 组 ≥2 实例 → submitInstanced（批上限 512 个矩阵，超出拆批）；恰 1 → 普通 submit；
    // AlphaBlend 一律普通 submit（透明段保持视距排序语义，禁实例化）。
    // 分组保序：以组内首个实例的遍历顺序提交（opaque 普通项与实例化批在 endFrame 中分段绘制）
    constexpr size_t kInstancedBatchLimit = 512;
    std::vector<bool> grouped(items.size(), false);
    for (size_t i = 0; i < items.size(); ++i) {
        if (grouped[i]) continue;
        grouped[i] = true;
        const SceneItem& base = items[i];
        if (base.material.blendMode != BlendMode::Opaque) {
            Renderer::submit(base.mesh, base.material, base.world);
            continue;
        }
        std::vector<glm::mat4> models{base.world};
        for (size_t j = i + 1; j < items.size(); ++j) {
            if (grouped[j] || items[j].material.blendMode != BlendMode::Opaque) continue;
            if (items[j].mesh == base.mesh && items[j].material == base.material) {
                grouped[j] = true;
                models.push_back(items[j].world);
            }
        }
        if (models.size() == 1) {
            Renderer::submit(base.mesh, base.material, base.world);
        } else {
            for (size_t b = 0; b < models.size(); b += kInstancedBatchLimit) {
                const size_t count = std::min(kInstancedBatchLimit, models.size() - b);
                std::vector<glm::mat4> batch(models.begin() + static_cast<std::ptrdiff_t>(b),
                                             models.begin() + static_cast<std::ptrdiff_t>(b + count));
                Renderer::submitInstanced(base.mesh, base.material, batch);
            }
        }
    }

    // ===== 遍历扩展点（§25.0 冻结语义，骨架日布；空实现默认 = 零影响）=====
    // 遍历结束后、阴影 pass 前：粒子（合并实例数据提交；排序张力 v1 备案——§25.4
    // 「粒子最后」与冻结钩子点的折中，如需调整由合波日评估一行移动）
    ParticleRenderer::collectAndSubmit(scene, renderCam);

    NE_GPU_SCOPE("shadow");   // V1.0-1 GPU 计时段（§26.0 骨架日；Profiler 关闭 = 零开销）
    if (shadowEnabled) Renderer::drawShadowPass(shadowParams, shadowItems);
    if (scene.showGrid) Renderer::drawGrid(scene.fogColor);

    // 地形（V0.5-8 2b 合波接线）：移至阴影 pass 之后绘制——阴影贴图此刻已渲染（地形可
    // 接收阴影，Renderer::setShadowUniforms）；深度缓冲此刻只有天空（z=1），地形写入深度
    // 后，endFrame 的不透明几何自然被地形正确遮挡（原钩子点在阴影前，深度/阴影均不可用）
    TerrainRenderer::collectAndSubmit(scene, renderCam, frustum, shadowItems);

    Renderer::endFrame();

    // V0.5-10 2D 精灵：透明段，全部 3D 之后绘制（(sortingLayer, orderInLayer, 视距) 排序后提交）
    drawSprites(scene, renderCam);
}

void SceneRenderer::renderScene(const Scene& scene, const RenderCamera& fallbackCamera) {
    // 旧路径（编辑器 Edit / 兼容调用）：固定非 HDR——uGamma=1/2.2，行为与波 2 逐像素一致
    // （本入口每帧重置 gamma 模式与 GBuffer 开关，防止与 renderSceneToOutput 的 HDR 帧
    // 交错时状态泄漏；GBuffer 恒关 = blinn_phong 不写 MRT 附件，逐像素与 2a 前一致）
    Renderer::setGammaMode(false);
    Renderer::setGbufferPassEnabled(false);
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
    // GBuffer pass 开关（V0.5-5 SSAO，§25.3 数据流）：hdr 且 ssaoEnabled 且 MRT 附件真实
    // 存在 → blinn_phong 写法线/线性深度附件（endFrame 据此把 draw buffer 切到 3 附件——
    // 必须与附件真实存在严格一致，否则 glDrawBuffers(3) 令单附件 FBO 不完整而黑屏）；
    // 其余（含非 hdr 直通）恒关 = 逐像素与 2a 前一致
    const bool gbufferPass = hdr && pp.ssaoEnabled &&
                             hdrFbo.normalTextureId() != 0 && hdrFbo.depthLinearTextureId() != 0;
    Renderer::setGbufferPassEnabled(gbufferPass);

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

    // ===== SSAO（V0.5-5，§25.3 数据流）：resolveMsaa 后、PostProcess::apply 前 =====
    // ssaoEnabled && hdr 且 MRT 附件可用 → SSAO::render（半分辨率 AO）供 combine 合成；
    // 否则 setAmbientOcclusionTexture(0)（combine 乘 1，AO 无贡献——非 hdr/附件缺失 = 降级路径）
    if (pp.ssaoEnabled && hdr && hdrFbo.normalTextureId() != 0 &&
        hdrFbo.depthLinearTextureId() != 0) {
        const uint32_t aoTex =
            SSAO::render(hdrFbo.normalTextureId(), hdrFbo.depthLinearTextureId(), pp);
        PostProcess::setAmbientOcclusionTexture(aoTex);
    } else {
        PostProcess::setAmbientOcclusionTexture(0);
    }

    // 后处理（或 RGBA8 直通 blit）→ UI（outLdrFbo 内，§14.3 渲染顺序：HDR→post→UI→显示）
    PostProcess::resize(hdrFbo.width(), hdrFbo.height());
    outLdrFbo.bind();
    {
        NE_GPU_SCOPE("post");   // V1.0-1 GPU 计时段（§26.0 骨架日；Profiler 关闭 = 零开销）
        PostProcess::apply(hdrFbo.colorTextureId(), pp);
    }
    NE_GPU_SCOPE("ui");   // V1.0-1 GPU 计时段（§26.0 骨架日；Profiler 关闭 = 零开销）
    UIRenderer::render(scene, outLdrFbo.width(), outLdrFbo.height());
    outLdrFbo.unbind();
    return outLdrFbo.colorTextureId();
}

uint32_t SceneRenderer::renderScenesToOutput(const std::vector<const Scene*>& scenes,
                                             const RenderCamera& fallbackCamera,
                                             Framebuffer& hdrFbo, Framebuffer& outLdrFbo,
                                             const PostProcessSettings& pp,
                                             const std::vector<OutlineItem>* outlines,
                                             const std::function<void()>& afterSceneDraw,
                                             bool useSceneCamera) {
    // ===== V1.0-2 骨架日实现（§27.1）：单场景 = 旧入口精确转发（逐像素零变化）=====
    if (scenes.empty()) return outLdrFbo.colorTextureId();
    if (scenes.size() == 1) {
        if (const Scene* s = scenes[0])
            return renderSceneToOutput(*s, fallbackCamera, hdrFbo, outLdrFbo, pp, outlines,
                                       afterSceneDraw, useSceneCamera);
        return outLdrFbo.colorTextureId();
    }
    // 多场景合并遍历（§27.1 渲染合并：跨场景剔除/阴影/透明排序/实例化分组统一收集）
    // 实现归 cR 波 2——骨架日临时：只渲染主场景并 warn 一次（行为可预期，不静默）
    static bool s_warned = false;
    if (!s_warned) {
        Logger::warn("[SceneRenderer] 多场景渲染合并尚未实现（cR 波 2 施工中），临时只渲染主场景");
        s_warned = true;
    }
    return renderSceneToOutput(*scenes[0], fallbackCamera, hdrFbo, outLdrFbo, pp, outlines,
                               afterSceneDraw, useSceneCamera);
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
