// ParticleRenderer.cpp —— 粒子渲染实现（V0.5-6，c4）：
// 遍历收集全部启用的 ParticleSystem → 按（贴图 × 混合模式 × 条网格）分组合并实例数据
// → 每组一次 VBO 上传 + 一次 glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count)。
// 典型场景（单火焰系统）恰好 1 次 draw call（§25.4 验收 2 口径）。
//
// 调用点 = 骨架日钩子（SceneRenderer 遍历后、阴影 pass 前，§25.0 冻结语义）：
// 本实现按钩子点立即绘制（排序张力见最终报告——透明几何排序由 c0 合波日裁定，
// 一行移动即可把调用点移到透明段之后）。
#include <NovaEngine/Particles/ParticleRenderer.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Particles/ParticleSystem.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>

#include <glad/gl.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace NovaEngine {

namespace {

// ===== GPU 资源（懒初始化：collectAndSubmit 首次调用创建。init/shutdown 当前无人接线——
// 编辑器/Player 均只在渲染帧内经由骨架日钩子调用 collectAndSubmit，此时 GL 上下文存活）=====
std::shared_ptr<Shader> s_shader;
uint32_t s_quadVao = 0, s_quadVbo = 0;   // 单位 quad（4 顶点三角形带，角坐标 -1..1）
uint32_t s_instVbo = 0;                  // 实例 VBO（每组一次整体上传，GL_STREAM_DRAW）
std::shared_ptr<Texture> s_whiteTex;     // 空贴图兜底 = 白块（1×1）
bool s_shaderFailedLogged = false;       // 着色器失败只记一次（避免每帧刷错误日志）

// 贴图强引用缓存：AssetDatabase 只做弱缓存——不在此持强引用的话，收集 lambda 返回后
// Texture 即析构（glDeleteTextures），下一帧弱缓存失效重载、GL 名抖动且绑定失败
// （实测 NVIDIA 上绑定已删除名直接 GL_INVALID_OPERATION → 粒子黑/不可见）。
// 加载失败也缓存 nullptr（避免每帧重试刷错误日志）；shutdown 清空。
std::unordered_map<std::string, std::shared_ptr<Texture>> s_texCache;

// ===== 内部统计（Renderer 的 lastParticleCount/lastParticleDrawCalls 目前只有 getter，
// 无 setter 通道——合波日 c0 补 reportParticleStats 后由本模块上报；本波自维护计数）=====
uint32_t s_lastCount = 0;       // 上一帧绘制粒子实例数
uint32_t s_lastDrawCalls = 0;   // 上一帧粒子 draw call 数（分组数）

// 每实例布局（与 particle.vert 的 location 1..4 对齐）：pos(3)+color(4)+size(1)+tile(1)
constexpr GLsizei kInstanceStride = 9 * static_cast<GLsizei>(sizeof(float));

// 分组键 = 贴图 × 混合模式 × 纹理条网格（同键合批；不同系统同贴图同混合 = 单 draw call）
struct ParticleGroup {
    uint32_t textureId = 0;      // 0 = 白块兜底
    bool additive = false;
    int tileX = 1, tileY = 1;
    std::vector<float> data;     // 交错实例数据（kInstanceStride/实例）
};

void ensureResources() {
    if (s_shader || s_shaderFailedLogged)
        return;
    s_shader = Shader::createFromFiles("assets/shaders/particle.vert",
                                       "assets/shaders/particle.frag");
    if (!s_shader) {
        s_shaderFailedLogged = true;
        Logger::error("[ParticleRenderer] 粒子着色器加载失败，粒子不绘制（不崩溃）");
        return;
    }
    if (!s_whiteTex)
        s_whiteTex = Texture::createWhite();

    // quad VAO：静态角坐标 VBO + 动态实例 VBO（attrib 1..4，divisor 1）
    glGenVertexArrays(1, &s_quadVao);
    glBindVertexArray(s_quadVao);
    glGenBuffers(1, &s_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_quadVbo);
    // 三角形带：(-1,-1),(1,-1),(-1,1),(1,1)——GL 对 strip 奇数三角形自动交换绕序，
    // 两三角形均 CCW 正面（面向相机可见，配合默认背面剔除）
    const float corners[4 * 2] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * static_cast<GLsizei>(sizeof(float)),
                          nullptr);

    glGenBuffers(1, &s_instVbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_instVbo);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kInstanceStride, nullptr);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kInstanceStride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, kInstanceStride,
                          reinterpret_cast<const void*>(7 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, kInstanceStride,
                          reinterpret_cast<const void*>(8 * sizeof(float)));
    glVertexAttribDivisor(1, 1);
    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    glVertexAttribDivisor(4, 1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}  // namespace

void ParticleRenderer::init() {
    // 显式接线入口（当前无人调用；collectAndSubmit 内部懒初始化兜底）。
    // 注意：需 GL 上下文存活，调用方须在 Renderer::init 之后调用。
    ensureResources();
}

void ParticleRenderer::shutdown() {
    if (s_quadVao) { glDeleteVertexArrays(1, &s_quadVao); s_quadVao = 0; }
    if (s_quadVbo) { glDeleteBuffers(1, &s_quadVbo); s_quadVbo = 0; }
    if (s_instVbo) { glDeleteBuffers(1, &s_instVbo); s_instVbo = 0; }
    s_shader.reset();
    s_whiteTex.reset();
    s_texCache.clear();
    s_shaderFailedLogged = false;
}

void ParticleRenderer::collectAndSubmit(const Scene& scene, const RenderCamera& camera) {
    s_lastCount = 0;
    s_lastDrawCalls = 0;
    ensureResources();
    if (!s_shader)
        return;

    // 1. 收集：全部 activeInHierarchy 的 ParticleSystem（与 MeshRenderer 同一活跃语义）
    //    → collectInstances（Play 实时池 / Edit 稳态快照）→ 分组合并实例数据
    std::vector<ParticleGroup> groups;
    scene.forEachObject([&](GameObject* go) {
        if (!go->activeInHierarchy()) return;
        const ParticleSystem* ps = go->getComponent<ParticleSystem>();
        if (!ps) return;

        std::vector<ParticleInstance> insts;
        ps->collectInstances(insts);
        if (insts.empty()) return;

        // 贴图解析：渲染器持强引用（AssetDatabase 仅弱缓存，见 s_texCache 注释）；
        // 加载失败 → 白块兜底（AssetDatabase 内部已记 error，此处不再重复）
        uint32_t texId = 0;
        if (!ps->texturePath.empty()) {
            auto it = s_texCache.find(ps->texturePath);
            if (it == s_texCache.end()) {
                it = s_texCache.emplace(ps->texturePath,
                                        AssetDatabase::instance().getTexture(ps->texturePath))
                         .first;
            }
            texId = it->second ? it->second->handle() : 0;
        }

        ParticleGroup* group = nullptr;
        for (ParticleGroup& cand : groups) {
            if (cand.textureId == texId && cand.additive == ps->blendAdditive &&
                cand.tileX == ps->tileX && cand.tileY == ps->tileY) {
                group = &cand;
                break;
            }
        }
        if (!group) {
            groups.push_back({});
            group = &groups.back();
            group->textureId = texId;
            group->additive = ps->blendAdditive;
            group->tileX = std::max(ps->tileX, 1);
            group->tileY = std::max(ps->tileY, 1);
        }

        group->data.reserve(group->data.size() + insts.size() * 9);
        for (const ParticleInstance& in : insts) {
            group->data.push_back(in.position.x);
            group->data.push_back(in.position.y);
            group->data.push_back(in.position.z);
            group->data.push_back(in.color.r);
            group->data.push_back(in.color.g);
            group->data.push_back(in.color.b);
            group->data.push_back(in.color.a);
            group->data.push_back(in.size);
            group->data.push_back(in.tile);
        }
        s_lastCount += static_cast<uint32_t>(insts.size());
    });
    if (s_lastCount == 0)
        return;

    // 2. 绘制（v1 按骨架日钩子点立即绘制；透明段语义 = 混合 + 不写深度，深度测试保持——
    //    当前帧深度缓冲尚无不透明几何，遮挡行为见最终报告「排序张力」）
    //    相机右/上向量 = view 矩阵第 1/2 列（正交相机基础；用于 billboard 展开）
    const glm::vec3 camRight(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
    const glm::vec3 camUp(camera.view[0][1], camera.view[1][1], camera.view[2][1]);

    s_shader->bind();
    s_shader->setVec3("uCamRight", camRight);
    s_shader->setVec3("uCamUp", camUp);
    s_shader->setInt("uTex", 0);

    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);   // 透明段：不写深度（深度测试保持——被已绘制几何正确遮挡）

    glBindVertexArray(s_quadVao);
    for (const ParticleGroup& group : groups) {
        if (group.data.empty()) continue;
        if (group.textureId != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, group.textureId);
        } else {
            s_whiteTex->bind(0);
        }
        glBlendFunc(GL_SRC_ALPHA, group.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        s_shader->setInt("uTileX", group.tileX);
        s_shader->setInt("uTileY", group.tileY);
        glBindBuffer(GL_ARRAY_BUFFER, s_instVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(group.data.size() * sizeof(float)),
                     group.data.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                              static_cast<GLsizei>(group.data.size() / 9));
        ++s_lastDrawCalls;
    }
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}  // namespace NovaEngine
