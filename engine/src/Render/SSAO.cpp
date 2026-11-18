// SSAO.cpp —— 屏幕空间 AO（V0.5-5，§25.3，c3 实现）
//
// 管线（半分辨率）：半球采样 AO pass（ssao.frag：读场景法线 RGBA16F + 线性深度
//   RGBA16F（view-space -z），经 CameraBlock UBO（binding 0）提取投影缩放重建视图
//   坐标——§25.3 冻结签名不含相机参数，故不经 C++ 传投影矩阵）→ 复用 post.vert +
//   post_blur.frag 高斯模糊（水平/垂直各一次，本模块自行编译 shader 实例，PostProcess
//   的实例为私有）→ 返回 AO 纹理 id 供 PostProcess::combine 在 unit 2 合成。
//
// 纹理链（resize/首帧懒建，RGBA16F，半分辨率）：
//   aoRaw(w/2) —— SSAO 采样目标 → blurH（水平模糊）→ blurV（垂直模糊，返回）
//
// 降级规则（「永不黑屏」同纪律）：shader/VAO 失败 → s_disabled，render 恒返回 0
//   （SceneRenderer 置 setAmbientOcclusionTexture(0) → combine 绑 1×1 白乘 1，AO 无贡献）；
//   尺寸无效 → 返回 0；正交相机/天空像素 → shader 内输出 AO=1。

#include <NovaEngine/Render/SSAO.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Shader.h>

#include <glad/gl.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>

#include <glm/vec3.hpp>

namespace NovaEngine {

namespace {

// ===== 内部状态（全部匿名命名空间，公共头零 GL）=====
bool s_initialized = false;
bool s_disabled = true;   // init 失败 → 全部入口安全 no-op

std::shared_ptr<Shader> s_ssaoShader;
std::shared_ptr<Shader> s_blurShader;
uint32_t s_fullscreenVao = 0;
uint32_t s_noiseTex = 0;   // 4x4 随机旋转（RGBA8，REPEAT + NEAREST）

// 半分辨率 AO 链纹理（各带渲染 FBO）
struct AoTex {
    uint32_t tex = 0;
    uint32_t fbo = 0;
    int w = 0;
    int h = 0;
};
AoTex s_aoRaw;    // SSAO 采样目标
AoTex s_aoBlurH;  // 水平模糊目标
AoTex s_aoBlurV;  // 垂直模糊目标（最终结果，render 返回）

int s_width = 0;   // resize 记录的全分辨率尺寸（render 以输入纹理尺寸为准自愈）
int s_height = 0;

// 采样数收缩到契约档位 8/16/32（§25.3；越界就近取档）
int clampSampleCount(int n) {
    n = std::clamp(n, 8, 32);
    if (n <= 12) return 8;
    if (n <= 24) return 16;
    return 32;
}

void createAoTex(int w, int h, AoTex& out) {
    out.w = std::max(w, 1);
    out.h = std::max(h, 1);
    glGenTextures(1, &out.tex);
    glBindTexture(GL_TEXTURE_2D, out.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, out.w, out.h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &out.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, out.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out.tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void destroyAoTex(AoTex& t) {
    if (t.tex) { glDeleteTextures(1, &t.tex); t.tex = 0; }
    if (t.fbo) { glDeleteFramebuffers(1, &t.fbo); t.fbo = 0; }
    t.w = t.h = 0;
}

// 半分辨率三纹理整组重建（尺寸不变则 no-op；render 首帧/resize 共用）
void ensureTextures(int w, int h) {
    if (s_aoRaw.tex != 0 && s_aoRaw.w == w && s_aoRaw.h == h) return;
    destroyAoTex(s_aoRaw);
    destroyAoTex(s_aoBlurH);
    destroyAoTex(s_aoBlurV);
    if (w <= 0 || h <= 0) return;
    createAoTex(w, h, s_aoRaw);
    createAoTex(w, h, s_aoBlurH);
    createAoTex(w, h, s_aoBlurV);
}

// 4x4 噪声纹理（随机切向旋转）：固定种子——噪声逐帧/逐次运行稳定（静止画面无跳动，验收 3）
bool createNoiseTexture() {
    std::mt19937 rng(20251118u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    uint8_t data[16 * 4];
    for (int i = 0; i < 16; ++i) {
        data[i * 4 + 0] = static_cast<uint8_t>((dist(rng) * 0.5f + 0.5f) * 255.0f);  // x ∈ [-1,1]
        data[i * 4 + 1] = static_cast<uint8_t>((dist(rng) * 0.5f + 0.5f) * 255.0f);  // y ∈ [-1,1]
        data[i * 4 + 2] = 128;   // z = 0（切平面内旋转）
        data[i * 4 + 3] = 255;
    }
    glGenTextures(1, &s_noiseTex);
    if (s_noiseTex == 0) return false;
    glBindTexture(GL_TEXTURE_2D, s_noiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

// 单方向高斯（复用 post_blur.frag：uDirection=(1,0)/(0,1) 水平/垂直；源纹理尺寸 = 目标尺寸）
void runBlur(uint32_t srcTex, const AoTex& dst, float dirX, float dirY) {
    s_blurShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    s_blurShader->setInt("uTexture", 0);
    s_blurShader->setVec3("uDirection", glm::vec3(dirX, dirY, 0.0f));
    s_blurShader->setVec3("uTexelSize", glm::vec3(1.0f / static_cast<float>(dst.w),
                                                  1.0f / static_cast<float>(dst.h), 0.0f));
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    glViewport(0, 0, dst.w, dst.h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace

// ==================== 公开接口 ====================

void SSAO::init() {
    if (s_initialized) return;

    // 全屏 VAO（无 VBO：post.vert 由 gl_VertexID 生成三角形）
    glGenVertexArrays(1, &s_fullscreenVao);
    if (s_fullscreenVao == 0) {
        Logger::warn("[SSAO] 全屏 VAO 创建失败，SSAO 不可用（combine 乘 1，不黑屏）");
        s_initialized = true;   // s_disabled 保持 true
        return;
    }

    bool ok = true;
    s_ssaoShader = Shader::createFromFiles("assets/shaders/post.vert",
                                           "assets/shaders/ssao.frag");
    s_blurShader = Shader::createFromFiles("assets/shaders/post.vert",
                                           "assets/shaders/post_blur.frag");
    if (!s_ssaoShader || !s_blurShader) {
        Logger::warn("[SSAO] 着色器加载失败，SSAO 降级关闭（combine 乘 1，不崩溃）");
        ok = false;
    }
    if (!createNoiseTexture()) {
        Logger::warn("[SSAO] 噪声纹理创建失败，SSAO 降级关闭");
        ok = false;
    }

    s_disabled = !ok;
    s_initialized = true;
}

void SSAO::shutdown() {
    if (!s_initialized) return;
    destroyAoTex(s_aoRaw);
    destroyAoTex(s_aoBlurH);
    destroyAoTex(s_aoBlurV);
    if (s_noiseTex) { glDeleteTextures(1, &s_noiseTex); s_noiseTex = 0; }
    if (s_fullscreenVao) { glDeleteVertexArrays(1, &s_fullscreenVao); s_fullscreenVao = 0; }
    s_ssaoShader.reset();
    s_blurShader.reset();
    s_width = s_height = 0;
    s_initialized = false;
    s_disabled = true;
}

void SSAO::resize(int w, int h) {
    s_width = w;
    s_height = h;
    if (!s_initialized || s_disabled) return;   // 未初始化/降级：不动纹理（render 会自愈/早退）
    if (w <= 0 || h <= 0) {   // 最小化/折叠：保持空，render 早退
        destroyAoTex(s_aoRaw);
        destroyAoTex(s_aoBlurH);
        destroyAoTex(s_aoBlurV);
        return;
    }
    ensureTextures(std::max(w / 2, 1), std::max(h / 2, 1));   // 半分辨率重建（懒建：init 不建）
}

uint32_t SSAO::render(uint32_t normalTex, uint32_t linearDepthTex, const PostProcessSettings& s) {
    if (normalTex == 0 || linearDepthTex == 0) return 0;
    if (!s_initialized) init();   // 懒初始化（幂等；编辑器/Player 入口未显式调用 init 时兜底）
    if (s_disabled) return 0;
    // 无效果参数：直接返回 0（调用方置 AO=0 → combine 乘 1，省两张模糊 pass）
    if (s.ssaoRadius <= 0.0f || s.ssaoStrength <= 0.0f) return 0;

    // ===== 状态保存（结束后全量恢复；只动 FBO/纹理绑定与必要的绘制状态）=====
    GLint prevDrawFbo = 0, prevReadFbo = 0, prevVao = 0, prevProgram = 0, prevActiveTex = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    GLint prevTex0 = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    const GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);

    // ===== 半分辨率纹理懒建/尺寸自愈：以输入深度纹理实际尺寸为准（resize 未调用时兜底）=====
    glBindTexture(GL_TEXTURE_2D, linearDepthTex);
    GLint tw = 0, th = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    if (tw <= 0 || th <= 0) {
        glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(prevTex0));
        glActiveTexture(static_cast<GLenum>(prevActiveTex));
        return 0;
    }
    ensureTextures(std::max(tw / 2, 1), std::max(th / 2, 1));
    if (s_aoRaw.tex == 0) {   // 重建失败防御（尺寸为 0 等）
        glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(prevTex0));
        glActiveTexture(static_cast<GLenum>(prevActiveTex));
        return 0;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(s_fullscreenVao);

    // ===== 1. SSAO 采样：法线(0) + 线性深度(1) + 噪声(2) → aoRaw（半分辨率）=====
    s_ssaoShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, normalTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, linearDepthTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s_noiseTex);
    s_ssaoShader->setInt("uNormalTex", 0);
    s_ssaoShader->setInt("uDepthTex", 1);
    s_ssaoShader->setInt("uNoiseTex", 2);
    s_ssaoShader->setFloat("uRadius", s.ssaoRadius);
    s_ssaoShader->setFloat("uStrength", s.ssaoStrength);
    s_ssaoShader->setInt("uSampleCount", clampSampleCount(s.ssaoSampleCount));
    s_ssaoShader->setVec3("uNoiseScale", glm::vec3(s_aoRaw.w / 4.0f, s_aoRaw.h / 4.0f, 0.0f));
    glBindFramebuffer(GL_FRAMEBUFFER, s_aoRaw.fbo);
    glViewport(0, 0, s_aoRaw.w, s_aoRaw.h);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ===== 2/3. 高斯模糊：水平 aoRaw→blurH，垂直 blurH→blurV（复用 post_blur.frag）=====
    runBlur(s_aoRaw.tex, s_aoBlurH, 1.0f, 0.0f);
    runBlur(s_aoBlurH.tex, s_aoBlurV, 0.0f, 1.0f);

    // ===== 状态恢复（与 PostProcess::apply 同约定：unit 1/2 复位 0，unit 0 还原）=====
    glBindVertexArray(static_cast<uint32_t>(prevVao));
    glUseProgram(static_cast<uint32_t>(prevProgram));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(prevTex0));
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<uint32_t>(prevDrawFbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<uint32_t>(prevReadFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND);

    return s_aoBlurV.tex;   // 供 PostProcess::combine 合成（unit 2，乘场景+bloom）
}

}  // namespace NovaEngine
