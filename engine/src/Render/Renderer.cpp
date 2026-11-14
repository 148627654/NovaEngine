#include <NovaEngine/Render/Renderer.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>     // 约定：先 glad 后 GLFW（GLFW_INCLUDE_NONE 已定义，顺序本身无冲突）
#include <GLFW/glfw3.h>

#include <algorithm>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

namespace NovaEngine {

namespace {

// ===== 渲染队列（submit 入队，endFrame 统一排序绘制并清空；IMPLEMENTATION-NOTES 裁定 6）=====
struct RenderItem {
    std::shared_ptr<Mesh> mesh;
    Material material;
    glm::mat4 model;
};
std::vector<RenderItem> s_queue;

// ===== 全局 GL 状态 =====
bool s_initialized = false;

uint32_t s_cameraUbo = 0;   // CameraBlock（binding 0：mat4 uViewProj + vec4 uCamPos）
std::shared_ptr<Shader> s_blinnPhong;
std::shared_ptr<Shader> s_gridShader;
std::shared_ptr<Shader> s_outlineShader;

uint32_t s_gridVao = 0;
uint32_t s_gridVbo = 0;
uint32_t s_gridVertexCount = 0;

// 当前帧相机/光照（beginFrame 填充，drawGrid / drawOutline / endFrame 使用）
glm::mat4 s_viewProj{1.0f};
glm::vec3 s_camPos{0.0f};
DirectionalLightData s_light;

// 网格地面配色：雾色与 Scene::skyColor 默认值一致（render 模块不 include scene 头，直接常量）
const glm::vec3 kGridFogColor(0.18f, 0.22f, 0.35f);
const glm::vec3 kGridLineColor(0.55f, 0.55f, 0.60f);

// ===== 内置网格地面（40x40m，1m 次线 / 5m 主线，GL_LINES 直接绘制）=====
void buildGrid() {
    std::vector<float> verts;   // 交错：x, y, z, major(0/1)
    verts.reserve(41 * 2 * 2 * 4);
    constexpr float kHalf = 20.0f;
    for (int i = 0; i <= 40; ++i) {
        const float p = -kHalf + static_cast<float>(i);
        const float major = (i % 5 == 0) ? 1.0f : 0.0f;
        // 沿 X 方向的线（固定 z）
        verts.insert(verts.end(), { -kHalf, 0.0f, p, major, kHalf, 0.0f, p, major });
        // 沿 Z 方向的线（固定 x）
        verts.insert(verts.end(), { p, 0.0f, -kHalf, major, p, 0.0f, kHalf, major });
    }

    glGenVertexArrays(1, &s_gridVao);
    glBindVertexArray(s_gridVao);
    glGenBuffers(1, &s_gridVbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);

    constexpr GLsizei kStride = 4 * static_cast<GLsizei>(sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    s_gridVertexCount = static_cast<uint32_t>(verts.size() / 4);
}

// 填充 blinn_phong 的材质/光照/模型 uniform（endFrame 逐项与 drawOutline 第一遍共用）
void setBlinnPhongUniforms(const std::shared_ptr<Shader>& shader,
                           const Material& material, const glm::mat4& model) {
    shader->setVec3("uLightDir", s_light.direction);
    shader->setVec3("uLightColor", s_light.color * s_light.intensity);
    shader->setVec3("uAmbient", s_light.ambient);
    shader->setVec3("uAlbedo", material.albedo);
    shader->setFloat("uRoughness", material.roughness);
    shader->setFloat("uMetallic", material.metallic);
    if (material.albedoMap) {
        material.albedoMap->bind(0);
        shader->setInt("uAlbedoMap", 0);
        shader->setInt("uUseAlbedoMap", 1);
    } else {
        shader->setInt("uUseAlbedoMap", 0);
    }
    shader->setMat4("uModel", model);
}

}  // namespace

void Renderer::init() {
    if (s_initialized) return;

    // glad 加载 GL 4.5 入口（窗口上下文已由 Window::create 设为当前）
    if (!gladLoadGL(glfwGetProcAddress)) {
        Logger::error("[Renderer] gladLoadGL 失败");
        return;
    }
    Logger::info("[Renderer] OpenGL 版本: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // 内置着色器（相对项目根路径；编辑器运行目录 = 项目根，见 ARCHITECTURE.md §9.4）
    s_blinnPhong = Shader::createFromFiles("assets/shaders/blinn_phong.vert",
                                           "assets/shaders/blinn_phong.frag");
    s_gridShader = Shader::createFromFiles("assets/shaders/grid.vert", "assets/shaders/grid.frag");
    s_outlineShader = Shader::createFromFiles("assets/shaders/outline.vert",
                                              "assets/shaders/outline.frag");

    // CameraBlock UBO（binding 0：mat4 uViewProj + vec4 uCamPos，std140）
    glGenBuffers(1, &s_cameraUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, s_cameraUbo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) + sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_cameraUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // 全局状态：深度测试 + 背面剔除
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    buildGrid();
    s_initialized = true;
}

void Renderer::shutdown() {
    if (!s_initialized) return;
    s_queue.clear();
    if (s_gridVao) { glDeleteVertexArrays(1, &s_gridVao); s_gridVao = 0; }
    if (s_gridVbo) { glDeleteBuffers(1, &s_gridVbo); s_gridVbo = 0; }
    if (s_cameraUbo) { glDeleteBuffers(1, &s_cameraUbo); s_cameraUbo = 0; }
    s_blinnPhong.reset();
    s_gridShader.reset();
    s_outlineShader.reset();
    s_initialized = false;
}

void Renderer::beginFrame(const RenderCamera& camera, const DirectionalLightData& light,
                          const glm::vec4& clearColor) {
    if (!s_initialized) return;

    // 恢复基础状态：ImGui 每帧渲染会关闭深度测试/剔除、开启混合与裁剪，这里重置干净
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // 清屏（颜色/深度/模板一并清除）
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // 每帧填充 CameraBlock UBO
    s_viewProj = camera.projection * camera.view;
    s_camPos = camera.position;
    s_light = light;

    glBindBuffer(GL_UNIFORM_BUFFER, s_cameraUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(s_viewProj));
    glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::vec4),
                    glm::value_ptr(glm::vec4(s_camPos, 1.0f)));
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void Renderer::submit(std::shared_ptr<Mesh> mesh, const Material& material,
                      const glm::mat4& modelMatrix) {
    if (!mesh) return;
    s_queue.push_back(RenderItem{std::move(mesh), material, modelMatrix});
}

void Renderer::endFrame() {
    if (!s_initialized) { s_queue.clear(); return; }

    // 按 albedoMap 句柄排序（同一 shader 下减少纹理绑定切换；无贴图排最前）
    std::stable_sort(s_queue.begin(), s_queue.end(), [](const RenderItem& a, const RenderItem& b) {
        const uint32_t ta = a.material.albedoMap ? a.material.albedoMap->handle() : 0;
        const uint32_t tb = b.material.albedoMap ? b.material.albedoMap->handle() : 0;
        return ta < tb;
    });

    if (s_blinnPhong) {
        s_blinnPhong->bind();
        // LEQUAL：允许与已写入深度相等的片段重绘。
        // drawOutline 第一遍已先绘过选中物体（写入深度），endFrame 重绘同深度物体必须用 LEQUAL，
        // 否则选中物体会以 drawOutline 的默认材质显示、真实材质被深度测试拒绝
        glDepthFunc(GL_LEQUAL);
        for (const RenderItem& item : s_queue) {
            setBlinnPhongUniforms(s_blinnPhong, item.material, item.model);
            item.mesh->draw();
        }
        glDepthFunc(GL_LESS);
    }

    s_queue.clear();
}

void Renderer::drawGrid() {
    if (!s_initialized || !s_gridShader || !s_gridVao) return;
    s_gridShader->bind();
    s_gridShader->setMat4("uViewProj", s_viewProj);
    s_gridShader->setVec3("uCamPos", s_camPos);
    s_gridShader->setVec3("uFogColor", kGridFogColor);
    s_gridShader->setVec3("uLineColor", kGridLineColor);
    glBindVertexArray(s_gridVao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(s_gridVertexCount));
    glBindVertexArray(0);
}

void Renderer::drawOutline(std::shared_ptr<Mesh> mesh, const glm::mat4& modelMatrix,
                           const glm::vec3& color) {
    if (!s_initialized || !mesh || !s_blinnPhong || !s_outlineShader) return;

    // 膨胀量 = 模型 AABB 最大边长的 4%（对象空间沿法线膨胀，随模型缩放）
    const AABB& box = mesh->aabb();
    const glm::vec3 extent = box.max - box.min;
    const float modelSize = std::max(extent.x, std::max(extent.y, extent.z));
    const float inflate = modelSize * 0.04f;

    // 第一遍：正常着色绘制，模板写 1（深度照写；ARCHITECTURE.md §3.4）。
    // 编辑器调用顺序为 submit → drawOutline → endFrame，此时物体尚未入深度缓冲，可正常通过深度测试
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);
    s_blinnPhong->bind();
    setBlinnPhongUniforms(s_blinnPhong, Material{}, modelMatrix);
    mesh->draw();

    // 第二遍：沿法线膨胀的描边壳，模板 != 1 处绘制（被遮挡部分也可见，Unity 风格）
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilMask(0x00);        // 描边不写模板
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);    // 画完整描边壳（否则背面被剔除，描边缺一半）
    s_outlineShader->bind();
    s_outlineShader->setMat4("uViewProj", s_viewProj);
    s_outlineShader->setMat4("uModel", modelMatrix);
    s_outlineShader->setFloat("uInflate", inflate);
    s_outlineShader->setVec3("uColor", color);
    mesh->draw();

    // 恢复（§3.4）
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
}

}  // namespace NovaEngine
