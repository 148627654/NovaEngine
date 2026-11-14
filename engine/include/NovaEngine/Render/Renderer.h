#pragma once
#include <memory>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include "Light.h"
#include "Material.h"
#include "Mesh.h"

namespace NovaEngine {

struct RenderCamera {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 position;
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
    static void drawGrid();                                 // 内置网格着色器（约 40x40m，主次线）
    static void drawOutline(std::shared_ptr<Mesh> mesh, const glm::mat4& modelMatrix,
                            const glm::vec3& color);        // stencil 两遍描边，立即绘制
    static void endFrame();     // 按 (shader, texture) 排序后统一绘制队列并清空
};

}  // namespace NovaEngine
