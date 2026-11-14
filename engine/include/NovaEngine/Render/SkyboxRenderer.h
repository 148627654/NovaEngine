#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <memory>

namespace NovaEngine {

class Cubemap;

// 天空模式（render 侧定义；Scene 环境字段引用它——scene → render 依赖方向合法）
enum class SkyMode { Gradient, Cubemap };

// 天空渲染（V0.2-7）：全屏三角形；渐变与 cubemap 采样共用一个片元着色器
class SkyboxRenderer {
public:
    static void init();       // 空 VAO（无顶点缓冲）+ skybox shader；失败仅 Logger::error 并置空
    static void shutdown();
    // 在 Renderer::beginFrame 之后、drawShadowPass 之前调用。
    // cubemap 为 nullptr 或 mode==Gradient 时走渐变路径；
    // 内部 glDepthFunc(GL_LEQUAL) 绘制、恢复 GL_LESS；深度写入 z=1.0（最远）。
    static void draw(const glm::mat4& invViewProj, const glm::vec3& camPos, SkyMode mode,
                     const glm::vec3& horizonColor, const glm::vec3& zenithColor,
                     std::shared_ptr<Cubemap> cubemap, float rotationDeg, float exposure = 1.0f);
};

}  // namespace NovaEngine
