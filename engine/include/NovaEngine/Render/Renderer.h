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
    static void beginFrame(const RenderCamera& camera, const DirectionalLightData& light,
                           const glm::vec4& clearColor);    // 清屏 + 填充每帧 UBO
    static void submit(std::shared_ptr<Mesh> mesh, const Material& material,
                       const glm::mat4& modelMatrix);       // 入队，不立即绘制
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
};

}  // namespace NovaEngine
