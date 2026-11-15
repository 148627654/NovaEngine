#include <NovaEngine/Render/Renderer.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>     // 约定：先 glad 后 GLFW（GLFW_INCLUDE_NONE 已定义，顺序本身无冲突）
#include <GLFW/glfw3.h>

#include <algorithm>
#include <iterator>
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
std::shared_ptr<Shader> s_shadowShader;   // 阴影深度 pass（V0.2-6）；加载失败置 nullptr，不崩溃
std::shared_ptr<Shader> s_debugLineShader;   // 物理碰撞体线框（V0.3-1）；失败置 nullptr，不崩溃
uint32_t s_debugLineVao = 0;                 // 线框 VAO/VBO（懒创建，动态顶点流）
uint32_t s_debugLineVbo = 0;

// ===== 阴影（V0.2-6）=====
ShadowMap s_shadowMap;                // 深度贴图（懒创建，mapSize 变化时重建）
ShadowCameraParams s_shadowParams;    // 本帧阴影参数（drawShadowPass 记录，endFrame/drawGrid 消费）
bool s_shadowActive = false;          // 本帧是否有可用阴影（beginFrame 重置）

// ===== 帧统计计数器（V0.2-11；beginFrame 清零，endFrame 结算到 s_last*）=====
uint32_t s_drawCalls = 0;           // 主 pass 绘制次数（队列项 + grid + outline 每次 2 遍）
uint32_t s_submitted = 0;           // submit() 次数
uint32_t s_tris = 0;                // 主 pass 累计三角形数（indexCount/3）
uint32_t s_shadowDrawCalls = 0;     // 阴影 pass 项数（不计入 s_drawCalls）
uint32_t s_lastDrawCalls = 0;
uint32_t s_lastSubmittedCount = 0;
uint32_t s_lastTriangleCount = 0;
uint32_t s_lastShadowItemCount = 0;
uint32_t s_lastTransparentCount = 0;   // 上一帧透明队列项数（V0.2-8）

uint32_t s_gridVao = 0;
uint32_t s_gridVbo = 0;
uint32_t s_gridVertexCount = 0;

// 当前帧相机/光照（beginFrame 填充，drawGrid / drawOutline / endFrame 使用）
glm::mat4 s_viewProj{1.0f};
glm::mat4 s_view{1.0f};      // 当前帧 view 矩阵（V0.2-8：透明队列 viewDepth 排序用）
glm::vec3 s_camPos{0.0f};
DirectionalLightData s_light;

// V0.3-14：后处理链 HDR 模式开关（uGamma=0 触发 shader 直通分支；由 SceneRenderer 每帧设置）
bool s_linearOutput = false;

// 网格地面配色：雾色已参数化（drawGrid(fogColor) 由 Scene::fogColor 传入，V0.2-5）
const glm::vec3 kGridLineColor(0.55f, 0.55f, 0.60f);

// 透明排序深度（裁定 2）：AABB 中心的视空间深度（负 Z 为前方，取负后远→大）
float viewDepthOf(const RenderItem& item) {
    const AABB& box = item.mesh->aabb();
    const glm::vec3 center = (box.min + box.max) * 0.5f;
    return -(s_view * (item.model * glm::vec4(center, 1.0f))).z;
}

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
    // V0.2-8：AlphaBlend 混合系数。Opaque 材质强制 alpha=1——alpha<1 且 blendMode=Opaque 时
    // 半透明 alpha 会写进 FBO 颜色附件 alpha 通道（不混合但值<1），导致视口图像幽灵半透明
    shader->setFloat("uAlpha", material.blendMode == BlendMode::Opaque ? 1.0f : material.alpha);
    if (material.albedoMap) {
        material.albedoMap->bind(0);
        shader->setInt("uAlbedoMap", 0);
        shader->setInt("uUseAlbedoMap", 1);
    } else {
        shader->setInt("uUseAlbedoMap", 0);
    }

    // ===== V0.3-4 PBR 贴图 =====
    // unit 总表（§14.4）：0 albedo / 1 shadow / 2 normal / 3 metallic / 4 roughness / 5 emission；
    // 贴图 null → 开关置 0（shader 侧走纯色/数值回退）
    if (material.normalMap) {
        material.normalMap->bind(2);
        shader->setInt("uNormalMap", 2);
        shader->setInt("uUseNormalMap", 1);
    } else {
        shader->setInt("uUseNormalMap", 0);
    }
    if (material.metallicMap) {
        material.metallicMap->bind(3);
        shader->setInt("uMetallicMap", 3);
        shader->setInt("uUseMetallicMap", 1);
    } else {
        shader->setInt("uUseMetallicMap", 0);
    }
    if (material.roughnessMap) {
        material.roughnessMap->bind(4);
        shader->setInt("uRoughnessMap", 4);
        shader->setInt("uUseRoughnessMap", 1);
    } else {
        shader->setInt("uUseRoughnessMap", 0);
    }
    if (material.emissionMap) {
        material.emissionMap->bind(5);
        shader->setInt("uEmissionMap", 5);
        shader->setInt("uUseEmissionMap", 1);
    } else {
        shader->setInt("uUseEmissionMap", 0);
    }
    shader->setVec3("uEmissionColor", material.emissionColor);
    shader->setFloat("uEmissionIntensity", material.emissionIntensity);

    // ===== V0.3-4/14 输出 gamma 指数 =====
    // 无后处理 = 1/2.2（V0.2 现行为）；HDR 模式 = 0（触发 shader 直通分支，非 pow 指数 0；
    // 波 3 后处理接管：SceneRenderer 经 setGammaMode 每帧设置，combine pass 内统一 gamma）
    shader->setFloat("uGamma", s_linearOutput ? 0.0f : 1.0f / 2.2f);

    shader->setMat4("uModel", model);

    // 阴影 uniform（V0.2-6）：有阴影时绑定深度贴图到 unit 1；无阴影只关开关
    if (s_shadowActive) {
        s_shadowMap.bindForReading(1);
        shader->setInt("uShadowMap", 1);
        shader->setInt("uShadowEnabled", 1);
        shader->setMat4("uLightViewProj", s_shadowParams.lightViewProj);
        shader->setFloat("uShadowBias", s_shadowParams.bias);
        shader->setFloat("uShadowMapSize", static_cast<float>(s_shadowParams.mapSize));
    } else {
        shader->setInt("uShadowEnabled", 0);
    }
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
    // 阴影着色器（V0.2-6）：失败只记一次错误并置空，阴影功能静默降级（不崩溃）
    s_shadowShader = Shader::createFromFiles("assets/shaders/shadow.vert",
                                             "assets/shaders/shadow.frag");
    if (!s_shadowShader)
        Logger::error("[Renderer] 阴影着色器加载失败，阴影功能不可用（不崩溃）");

    // 调试线框着色器（V0.3-1 物理碰撞体）：CameraBlock UBO（binding 0）复用 + uColor uniform
    s_debugLineShader = Shader::createFromFiles("assets/shaders/debug_line.vert",
                                                "assets/shaders/debug_line.frag");
    if (!s_debugLineShader)
        Logger::error("[Renderer] debug_line 着色器加载失败，物理线框不可用（不崩溃）");

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
    s_shadowMap.destroy();
    s_shadowShader.reset();
    s_debugLineShader.reset();
    if (s_debugLineVao) { glDeleteVertexArrays(1, &s_debugLineVao); s_debugLineVao = 0; }
    if (s_debugLineVbo) { glDeleteBuffers(1, &s_debugLineVbo); s_debugLineVbo = 0; }
    s_shadowActive = false;
    s_initialized = false;
}

void Renderer::setGammaMode(bool linearOutput) {
    s_linearOutput = linearOutput;   // 粘性：由 SceneRenderer 每帧设置（beginFrame 不重置）
}

bool Renderer::linearOutput() {
    return s_linearOutput;
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

    // 帧统计清零（V0.2-11）：阴影激活标志与全部计数器每帧重置
    s_shadowActive = false;
    s_drawCalls = 0;
    s_submitted = 0;
    s_tris = 0;
    s_shadowDrawCalls = 0;

    // 每帧填充 CameraBlock UBO
    s_viewProj = camera.projection * camera.view;
    s_view = camera.view;   // V0.2-8：透明队列 viewDepth 排序用
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
    ++s_submitted;
}

void Renderer::drawShadowPass(const ShadowCameraParams& shadow,
                              const std::vector<ShadowItem>& items) {
    // 任何 no-op 路径都不得残留上一帧的阴影状态
    s_shadowActive = false;
    if (!s_initialized || !s_shadowShader || items.empty() || shadow.mapSize <= 0)
        return;   // 着色器加载失败 / 空场景 / 非法尺寸：静默跳过（不崩溃）

    // 懒创建：mapSize 变化时重建（Inspector 改分辨率当帧生效）
    if (s_shadowMap.size() != shadow.mapSize && !s_shadowMap.create(shadow.mapSize))
        return;   // 创建失败：本帧无阴影

    // 保存当前 FBO 与视口（结束后恢复，调用方通常是编辑器 FBO）
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    s_shadowMap.bindForWriting();
    glClear(GL_DEPTH_BUFFER_BIT);

    // 深度 pass 状态：正面剔除（GL_FRONT）防 acne——只把背面的深度写入贴图
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glDisable(GL_STENCIL_TEST);

    s_shadowShader->bind();
    s_shadowShader->setMat4("uLightViewProj", shadow.lightViewProj);
    for (const ShadowItem& item : items) {
        if (!item.mesh) continue;
        s_shadowShader->setMat4("uModel", item.model);
        item.mesh->draw();   // 复用 Mesh VAO（shadow.vert 顶点属性布局与 Mesh 交错布局一致）
        ++s_shadowDrawCalls;
    }

    // 恢复状态与 FBO/视口
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<uint32_t>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    s_shadowParams = shadow;
    s_shadowActive = true;
}

void Renderer::endFrame() {
    uint32_t transparentCount = 0;
    if (s_initialized) {
        // 裁定 2：stable_partition 按 blendMode 分两段——opaque 在前、transparent 在后
        const auto transparentBegin = std::stable_partition(
            s_queue.begin(), s_queue.end(),
            [](const RenderItem& item) { return item.material.blendMode == BlendMode::Opaque; });
        transparentCount = static_cast<uint32_t>(std::distance(transparentBegin, s_queue.end()));

        // opaque 段：保持现有 albedoMap 句柄排序（减少纹理绑定切换；无贴图排最前）
        std::stable_sort(s_queue.begin(), transparentBegin,
                         [](const RenderItem& a, const RenderItem& b) {
            const uint32_t ta = a.material.albedoMap ? a.material.albedoMap->handle() : 0;
            const uint32_t tb = b.material.albedoMap ? b.material.albedoMap->handle() : 0;
            return ta < tb;
        });

        // transparent 段：viewDepth 降序（远→近，画家算法后→前绘制）
        std::sort(transparentBegin, s_queue.end(), [](const RenderItem& a, const RenderItem& b) {
            return viewDepthOf(a) > viewDepthOf(b);
        });

        if (s_blinnPhong) {
            s_blinnPhong->bind();
            // LEQUAL：允许与已写入深度相等的片段重绘。
            // drawOutline 第一遍已先绘过选中物体（写入深度），endFrame 重绘同深度物体必须用 LEQUAL，
            // 否则选中物体会以 drawOutline 的默认材质显示、真实材质被深度测试拒绝
            glDepthFunc(GL_LEQUAL);

            // opaque 照旧绘制（深度照写）
            for (auto it = s_queue.begin(); it != transparentBegin; ++it) {
                setBlinnPhongUniforms(s_blinnPhong, it->material, it->model);
                it->mesh->draw();
                s_tris += it->mesh->indexCount() / 3;
                ++s_drawCalls;
            }

            // transparent：标准 alpha 混合 + 禁深度写（深度仍测试，保证被 opaque 正确遮挡）
            if (transparentCount > 0) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                for (auto it = transparentBegin; it != s_queue.end(); ++it) {
                    setBlinnPhongUniforms(s_blinnPhong, it->material, it->model);
                    it->mesh->draw();
                    s_tris += it->mesh->indexCount() / 3;
                    ++s_drawCalls;
                }
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }

            glDepthFunc(GL_LESS);
        }
    }

    s_queue.clear();

    // 结算上一帧统计（V0.2-11）：last* 访问器返回的都是上一帧值（透明项数同一结算点）
    s_lastDrawCalls = s_drawCalls;
    s_lastSubmittedCount = s_submitted;
    s_lastTriangleCount = s_tris;
    s_lastShadowItemCount = s_shadowDrawCalls;
    s_lastTransparentCount = transparentCount;
}

void Renderer::drawGrid(const glm::vec3& fogColor) {
    if (!s_initialized || !s_gridShader || !s_gridVao) return;
    s_gridShader->bind();
    s_gridShader->setMat4("uViewProj", s_viewProj);
    s_gridShader->setVec3("uCamPos", s_camPos);
    s_gridShader->setVec3("uFogColor", fogColor);   // V0.2-5：雾色参数化（Scene::fogColor）
    s_gridShader->setVec3("uLineColor", kGridLineColor);
    s_gridShader->setVec3("uLightDir", s_light.direction);
    // V0.3-4/14：与 blinn_phong 同款 gamma 指数（无后处理 = 1/2.2；HDR 模式 = 0 触发直通分支；波 3 后处理接管）
    s_gridShader->setFloat("uGamma", s_linearOutput ? 0.0f : 1.0f / 2.2f);

    // 地面也接收阴影（V0.2-6）：与 setBlinnPhongUniforms 同一套 uniform 约定
    if (s_shadowActive) {
        s_shadowMap.bindForReading(1);
        s_gridShader->setInt("uShadowMap", 1);
        s_gridShader->setInt("uShadowEnabled", 1);
        s_gridShader->setMat4("uLightViewProj", s_shadowParams.lightViewProj);
        s_gridShader->setFloat("uShadowBias", s_shadowParams.bias);
        s_gridShader->setFloat("uShadowMapSize", static_cast<float>(s_shadowParams.mapSize));
    } else {
        s_gridShader->setInt("uShadowEnabled", 0);
    }

    glBindVertexArray(s_gridVao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(s_gridVertexCount));
    glBindVertexArray(0);
    ++s_drawCalls;
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

    s_drawCalls += 2;   // 描边两遍各计一次
}

void Renderer::drawLines(const std::vector<glm::vec3>& positions, const glm::vec3& color) {
    // 调试线框（V0.3-1 物理碰撞体）：positions 为顶点对序列（a,b 成对，GL_LINES）。
    // 3D pass 之后调用；CameraBlock UBO（binding 0，beginFrame 已填充）复用为 uViewProj。
    if (!s_initialized || !s_debugLineShader || positions.size() < 2)
        return;

    // 懒创建线框 VAO/VBO（动态顶点流，每帧整体上传——碰撞体线框规模小）
    if (s_debugLineVao == 0) {
        glGenVertexArrays(1, &s_debugLineVao);
        glBindVertexArray(s_debugLineVao);
        glGenBuffers(1, &s_debugLineVbo);
        glBindBuffer(GL_ARRAY_BUFFER, s_debugLineVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glBindVertexArray(0);
    }

    s_debugLineShader->bind();
    s_debugLineShader->setVec3("uColor", color);
    glBindVertexArray(s_debugLineVao);
    glBindBuffer(GL_ARRAY_BUFFER, s_debugLineVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(positions.size() * sizeof(glm::vec3)),
                 positions.data(), GL_STREAM_DRAW);
    glLineWidth(2.0f);   // 碰撞体线宽 2px（深度测试保持开启：线框被场景几何正确遮挡）
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(positions.size()));
    glBindVertexArray(0);
    ++s_drawCalls;
}

// ===== 统计访问器（V0.2-11）：返回上一帧结算值 =====

uint32_t Renderer::lastDrawCalls() { return s_lastDrawCalls; }
uint32_t Renderer::lastSubmittedCount() { return s_lastSubmittedCount; }
uint32_t Renderer::lastTriangleCount() { return s_lastTriangleCount; }
uint32_t Renderer::lastShadowItemCount() { return s_lastShadowItemCount; }
uint32_t Renderer::lastTransparentCount() { return s_lastTransparentCount; }

}  // namespace NovaEngine
