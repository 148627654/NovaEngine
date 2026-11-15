// SkyboxRenderer.cpp —— 天空渲染（V0.2-7）
//
// 全屏三角形（空 VAO + gl_VertexID，无顶点缓冲），z=w=1.0 深度最远 + GL_LEQUAL：
// 天空只画在已清屏区域，不遮挡任何物体。渐变与 cubemap 共用一个片元着色器（uUseCubemap 分支）。

#include <NovaEngine/Render/SkyboxRenderer.h>
#include <NovaEngine/Render/Cubemap.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>   // 约定：先 glad 后 GLFW
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>   // glm::radians（与 SceneViewPanel 同源链）

namespace NovaEngine {

namespace {

uint32_t s_vao = 0;                 // 空 VAO：全屏三角形顶点由 gl_VertexID 生成
std::shared_ptr<Shader> s_shader;   // skybox shader；加载失败置空，draw 静默跳过
bool s_initialized = false;

}  // namespace

void SkyboxRenderer::init() {
    if (s_initialized) return;

    // 空 VAO（无 VBO）：GL 4.5 core 允许无启用顶点属性时用 gl_VertexID 绘制
    glGenVertexArrays(1, &s_vao);

    s_shader = Shader::createFromFiles("assets/shaders/skybox.vert",
                                       "assets/shaders/skybox.frag");
    if (!s_shader)
        Logger::error("[SkyboxRenderer] 天空着色器加载失败，天空渲染不可用（不崩溃）");

    s_initialized = true;
}

void SkyboxRenderer::shutdown() {
    if (!s_initialized) return;
    if (s_vao) { glDeleteVertexArrays(1, &s_vao); s_vao = 0; }
    s_shader.reset();
    s_initialized = false;
}

void SkyboxRenderer::draw(const glm::mat4& invViewProj, const glm::vec3& camPos, SkyMode mode,
                          const glm::vec3& horizonColor, const glm::vec3& zenithColor,
                          std::shared_ptr<Cubemap> cubemap, float rotationDeg, float exposure) {
    if (!s_initialized || !s_shader) return;

    // camPos 未直接使用：skybox.vert 从 CameraBlock UBO（beginFrame 已填）读取 uCamPos
    (void)camPos;
    const bool useCubemap = (mode == SkyMode::Cubemap) && (cubemap != nullptr);

    s_shader->bind();
    s_shader->setMat4("uInvViewProj", invViewProj);
    s_shader->setInt("uUseCubemap", useCubemap ? 1 : 0);
    s_shader->setVec3("uHorizonColor", horizonColor);
    s_shader->setVec3("uZenithColor", zenithColor);
    s_shader->setFloat("uSkyExposure", exposure);                    // HDR 曝光（Scene::skyExposure）
    s_shader->setFloat("uSkyRotation", glm::radians(rotationDeg));   // 度 → 弧度（C++ 侧转换）
    // V0.3-4：与 blinn_phong 同款 gamma 指数（无后处理 = 1/2.2；HDR 模式 = 0 触发直通分支，非 pow 指数 0；波 3 后处理接管）
    s_shader->setFloat("uGamma", 1.0f / 2.2f);
    if (useCubemap) {
        cubemap->bind(1);
        s_shader->setInt("uSkyCubemap", 1);
    }

    // 深度 z=1.0（最远）：LEQUAL 使天空只在清屏区域绘制；画完恢复 LESS
    glDisable(GL_CULL_FACE);   // 防御：不依赖全屏三角形的绕序
    glDepthFunc(GL_LEQUAL);
    glBindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
}

}  // namespace NovaEngine
