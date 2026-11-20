#pragma once
#include <memory>
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "ShadowMap.h"

namespace NovaEngine {
class Shader;   // setShadowUniforms 参数（实现于 Renderer.cpp）
}  // namespace NovaEngine

namespace NovaEngine {

struct RenderCamera {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 position;
};

// 阴影深度 pass 收集项（编辑器粘合层组装后传入 drawShadowPass）
struct ShadowItem {
    std::shared_ptr<Mesh> mesh;
    glm::mat4 model;
};

// 前向渲染器（全静态，内部渲染队列实现于 Renderer.cpp 匿名命名空间）
class Renderer {
public:
    static void init();       // gladLoadGL(glfwGetProcAddress) + 全局状态（深度测试/背面剔除/UBO）
    static void shutdown();
    static bool isInitialized();   // V1.0 波 1 骨架日（§26.0 冻结项 2）：GL 就绪查询（Mesh/Primitives 守卫用）
    static void beginFrame(const RenderCamera& camera, const DirectionalLightData& light,
                           const glm::vec4& clearColor);    // 清屏 + 填充每帧 UBO
    static void submit(std::shared_ptr<Mesh> mesh, const Material& material,
                       const glm::mat4& modelMatrix);       // 入队，不立即绘制
    // 入队实例化批（V0.5-14）：endFrame 中在 opaque 普通项之后、transparent 之前统一绘制；
    // 批内矩阵按一次 glDrawElementsInstanced 提交（1 draw call）
    static void submitInstanced(std::shared_ptr<Mesh> mesh, const Material& material,
                                const std::vector<glm::mat4>& modelMatrices);
    static void drawGrid(const glm::vec3& fogColor = glm::vec3(0.18f, 0.22f, 0.35f));  // 内置网格着色器（约 40x40m，主次线；雾色参数化 V0.2-5）
    static void drawOutline(std::shared_ptr<Mesh> mesh, const glm::mat4& modelMatrix,
                            const glm::vec3& color);        // stencil 两遍描边，立即绘制
    static void endFrame();     // 按 (shader, texture) 排序后统一绘制队列并清空

    // V0.3-14（波 3 后处理）：linearOutput=true 时场景着色器 uGamma=0（直通分支，场景线性输出，
    // 后处理 combine 内统一做 ACES+gamma）；false = 现值 1/2.2（旧路径）。由 SceneRenderer 每帧设置。
    static void setGammaMode(bool linearOutput);
    static bool linearOutput();   // 查询当前帧模式（Skybox 等旁路着色器同步 uGamma 用）

    // 阴影深度 prepass（V0.2-6）：beginFrame 之后、drawGrid/endFrame 之前调用一次。
    // 内部：懒创建 ShadowMap（mapSize 变化时重建）、切 FBO、清深度、glCullFace(GL_FRONT) 绘制、
    // 恢复原 FBO 与视口；记录阴影参数供 endFrame/drawGrid 绑定阴影纹理。
    // shader 加载失败或 items 为空时静默 no-op（无灯光的空场景不崩溃）。
    static void drawShadowPass(const ShadowCameraParams& shadow,
                               const std::vector<ShadowItem>& items);

    // 统计访问器（V0.2-11；返回上一帧结算值）
    static uint32_t lastDrawCalls();        // 主 pass 绘制次数（endFrame 项数 + grid + outline 每次 2 遍）
    static uint32_t lastSubmittedCount();   // 上一帧 submit() 次数
    static uint32_t lastTriangleCount();    // 上一帧主 pass 累计三角形数（indexCount/3）
    static uint32_t lastShadowItemCount();  // 上一帧阴影 pass 项数（不计入 lastDrawCalls）
    static uint32_t lastTransparentCount(); // 上一帧透明队列项数（V0.2-8）
    // 调试线框（V0.3-1 物理碰撞体）：positions 为顶点对序列（a,b 成对）；3D pass 后绘制
    static void drawLines(const std::vector<glm::vec3>& positions, const glm::vec3& color);

    // ===== V0.5-14 视锥剔除 + 实例化统计 =====
    static void setFrustumCullingEnabled(bool enabled);   // 默认 true；false 时遍历行为与波 4 基线一致
    static bool frustumCullingEnabled();
    static void markCulled();                             // 剔除计数 +1（SceneRenderer 剔除遍历上报）
    static uint32_t lastCulledCount();                    // 上一帧剔除项数
    static uint32_t lastInstancedDrawCalls();             // 上一帧实例化 draw call 数（批数）
    static uint32_t lastInstancedInstanceCount();         // 上一帧实例化累计实例数

    // ===== 波 2 渲染钩子（V0.5-4/5/6，骨架日 c0 布；追加式）=====
    // IBL 环境绑定（§25.2）：irradiance cubemap（unit 6）/ prefiltered cubemap（unit 7）/
    // BRDF LUT（unit 8）+ 强度；全 0 = 无环境（blinn_phong.frag 间接光贡献 0，逐像素与 2a 前一致）。
    // 调用点 = SceneRenderer 天空段（每帧；同源 HDRI 或渐变天空均会调用）。
    static void setEnvironment(uint32_t irradianceMapId, uint32_t prefilterMapId,
                               uint32_t brdfLutId, float intensity);
    // 天空旋转对齐（§25.2 合波补）：blinn_phong.frag 的 IBL 采样方向绕 +Y 旋转（弧度），
    // 与 SkyboxRenderer 同款变换；由 SceneRenderer 天空段每帧设置（默认 0 = 不转）
    static void setIblSkyRotation(float radians);
    // 阴影 uniform 统一出口（§25.6 合波补）：为自含光照 shader（地形）绑定阴影贴图到
    // unit 1 + uShadowEnabled/uLightViewProj/uShadowBias/uShadowMapSize（与
    // setBlinnPhongUniforms 同款）；本帧无阴影 pass 时仅 uShadowEnabled=0
    static void setShadowUniforms(const std::shared_ptr<Shader>& shader);
    // SSAO GBuffer pass 开关（§25.3）：true 时 blinn_phong.frag 写 MRT 附件 1（法线）/附件 2（线性深度）；
    // 默认 false = 逐像素与 2a 前一致。由 SceneRenderer 每帧按 (hdr && ssaoEnabled) 设置。
    static void setGbufferPassEnabled(bool enabled);
    // 粒子统计（§25.4）：上一帧粒子数 / 粒子 draw call 数（空实现默认 0；ParticleRenderer 接入后填充）
    static uint32_t lastParticleCount();
    static uint32_t lastParticleDrawCalls();
    // 粒子统计上报（§25.4 合波补）：由 ParticleRenderer 每帧末回写（0 = 本帧无粒子/未接入）
    static void reportParticleStats(uint32_t count, uint32_t drawCalls);
};

}  // namespace NovaEngine
