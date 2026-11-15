// PostProcess.cpp —— 后处理链（V0.3-14，波 3，§14.10）
//
// 管线：brightpass（阈值软膝 + 2x2 降采样 → 半分辨率）→ 9-tap 高斯 ping-pong
//   （水平/垂直双向，半分辨率）→ 逐级降采样模糊（1/4 → 1/8，stride 2 单方向，
//   前级已各向同性）→ combine（scene + bloom×intensity → Narkowicz ACES →
//   exposure → gamma 1/2.2）输出到调用方当前绑定 FBO。
//
// 纹理链（resize 重建，RGBA16F）：
//   bright(w/2) + blur0(w/2) —— L0 水平/垂直 ping-pong 对（bright 兼作 brightpass 目标）
//   blur1(w/4)、blur2(w/8) —— 降采样级；uBloom = 按 bloomLevels 选 1..3 级的末级
//   （各级内容经链式降采样折叠进末级；combine 单 uBloom 输入，见冻结 shader 契约）
//
// 降级规则：init 任一 shader/VAO 失败 → s_disabled，全部入口早退（hdrSupported 亦返回
// false → 调用方回退 RGBA8 直通路径）；pp.enabled=false 时 apply 走 glBlitFramebuffer
// 直通（不依赖 shader，init 失败仍可用，保证 LDR 输出永不黑屏）。

#include <NovaEngine/Render/PostProcess.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Shader.h>

#include <glad/gl.h>

#include <algorithm>
#include <memory>

namespace NovaEngine {

namespace {

// ===== 内部状态（全部匿名命名空间，公共头零 GL）=====
bool s_initialized = false;
bool s_disabled = true;   // init 失败 → 强制降级（全部入口早退）

std::shared_ptr<Shader> s_brightShader;
std::shared_ptr<Shader> s_blurShader;
std::shared_ptr<Shader> s_combineShader;
uint32_t s_fullscreenVao = 0;

// 半分辨率链纹理（各带渲染 FBO）
struct BloomTex {
    uint32_t tex = 0;
    uint32_t fbo = 0;
    int w = 0;
    int h = 0;
};
BloomTex s_bright;   // w/2（brightpass 目标 + L0 垂直 ping-pong 目标）
BloomTex s_blur0;    // w/2（L0 水平模糊目标）
BloomTex s_blur1;    // w/4
BloomTex s_blur2;    // w/8
int s_width = 0;     // 记录 resize 尺寸（apply 直通 blit 的源尺寸）
int s_height = 0;

uint32_t s_copyFbo = 0;   // 直通 blit 的 READ 挂载点（懒创建；不依赖 shader）

// RGB16F 探测缓存（一次性）
bool s_hdrProbed = false;
bool s_hdr = false;

void createBloomTex(int w, int h, BloomTex& out) {
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

void destroyBloomTex(BloomTex& t) {
    if (t.tex) { glDeleteTextures(1, &t.tex); t.tex = 0; }
    if (t.fbo) { glDeleteFramebuffers(1, &t.fbo); t.fbo = 0; }
    t.w = t.h = 0;
}

// 绑定目标纹理为渲染目标并绘制全屏三角形
void drawFullscreenInto(const BloomTex& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.w, target.h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// 9-tap 高斯单方向（uDirection=(1,0)/(0,1) 水平/垂直；(2,0) = stride 2 降采样级）
void runBlur(uint32_t srcTex, int srcW, int srcH, const BloomTex& dst,
             float dirX, float dirY) {
    s_blurShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    s_blurShader->setInt("uTexture", 0);
    s_blurShader->setVec3("uDirection", glm::vec3(dirX, dirY, 0.0f));
    s_blurShader->setVec3("uTexelSize", glm::vec3(1.0f / static_cast<float>(srcW),
                                                  1.0f / static_cast<float>(srcH), 0.0f));
    drawFullscreenInto(dst);
}

// 直通 blit：hdrTex → 当前绑定 FBO（GL_READ 临时挂载；不依赖 shader，降级可用）
void blitPassthrough(uint32_t srcTex) {
    // 评审 P2-3：恢复入口 READ FBO 值而非 0（防御级：当前调用方均无非零 READ 绑定）
    GLint prevReadFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    if (s_copyFbo == 0)
        glGenFramebuffers(1, &s_copyFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s_copyFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    glBlitFramebuffer(0, 0, s_width, s_height, vp[0], vp[1], vp[2], vp[3],
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<uint32_t>(prevReadFbo));
}

}  // namespace

// ==================== 公开接口 ====================

void PostProcess::init() {
    if (s_initialized) return;

    // 全屏 VAO（无 VBO：post.vert 由 gl_VertexID 生成三角形）
    glGenVertexArrays(1, &s_fullscreenVao);
    if (s_fullscreenVao == 0) {
        Logger::warn("[PostProcess] 全屏 VAO 创建失败，后处理降级为直通");
        return;   // s_disabled 保持 true
    }

    bool ok = true;
    s_brightShader = Shader::createFromFiles("assets/shaders/post.vert",
                                             "assets/shaders/post_brightpass.frag");
    s_blurShader = Shader::createFromFiles("assets/shaders/post.vert",
                                           "assets/shaders/post_blur.frag");
    s_combineShader = Shader::createFromFiles("assets/shaders/post.vert",
                                              "assets/shaders/post_combine.frag");
    if (!s_brightShader || !s_blurShader || !s_combineShader) {
        Logger::warn("[PostProcess] 后处理 shader 加载失败，后处理降级为直通（不崩溃）");
        ok = false;
    }

    s_disabled = !ok;
    s_initialized = true;
}

void PostProcess::shutdown() {
    if (!s_initialized) return;
    destroyBloomTex(s_bright);
    destroyBloomTex(s_blur0);
    destroyBloomTex(s_blur1);
    destroyBloomTex(s_blur2);
    if (s_copyFbo) { glDeleteFramebuffers(1, &s_copyFbo); s_copyFbo = 0; }
    if (s_fullscreenVao) { glDeleteVertexArrays(1, &s_fullscreenVao); s_fullscreenVao = 0; }
    s_brightShader.reset();
    s_blurShader.reset();
    s_combineShader.reset();
    s_width = s_height = 0;
    s_initialized = false;
    s_disabled = true;
}

void PostProcess::resize(int w, int h) {
    // 尺寸始终记录（apply 直通 blit 的源尺寸；降级时纹理不建但 blit 仍可用）
    if (w == s_width && h == s_height) return;
    s_width = w;
    s_height = h;
    if (s_disabled) return;

    destroyBloomTex(s_bright);
    destroyBloomTex(s_blur0);
    destroyBloomTex(s_blur1);
    destroyBloomTex(s_blur2);
    if (w <= 0 || h <= 0) return;   // 最小化/折叠：保持空，调用方跳过渲染
    createBloomTex(w / 2, h / 2, s_bright);
    createBloomTex(w / 2, h / 2, s_blur0);
    createBloomTex(w / 4, h / 4, s_blur1);
    createBloomTex(w / 8, h / 8, s_blur2);
}

bool PostProcess::hdrSupported() {
    if (!s_initialized)
        init();   // 懒初始化：编辑器/Player 任一入口首次查询即完成 init（幂等）
    if (s_disabled)
        return false;   // shader 不可用 → 让调用方回退 RGBA8 直通（输出永不黑屏）
    if (!s_hdrProbed) {
        // 一次性探测：小 RGBA16F 纹理 + FBO 完整性检查（老集显可能不支持可渲染 16F）
        uint32_t tex = 0, fbo = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 8, 8, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        s_hdr = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        if (!s_hdr)
            Logger::warn("[PostProcess] RGBA16F 渲染目标不受支持，后处理降级为 RGBA8 直通");
        s_hdrProbed = true;
    }
    return s_hdr;
}

void PostProcess::apply(uint32_t hdrColorTexture, const PostProcessSettings& s) {
    if (s_width <= 0 || s_height <= 0)
        return;
    // 降级统一判定（「永不黑屏」契约，评审 P1-2）：enabled=false / shader 缺失 / 未初始化 /
    // GL 不支持可渲染 RGBA16F → 一律纯 blit 直通（不依赖 shader，也不依赖 bloom FBO 完整性）。
    // hdrSupported() 内含懒 init 与一次性探测，此处直接复用其结论（探测后为纯缓存读）
    if (!s.enabled || s_disabled || !s_initialized || !hdrSupported()) {
        blitPassthrough(hdrColorTexture);
        return;
    }

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

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(s_fullscreenVao);

    // ===== 1. brightpass：场景(全分辨率) → bright(w/2)，阈值软截断 =====
    s_brightShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColorTexture);
    s_brightShader->setInt("uTexture", 0);
    s_brightShader->setFloat("uThreshold", std::max(s.bloomThreshold, 0.0f));
    s_brightShader->setVec3("uTexelSize", glm::vec3(1.0f / static_cast<float>(s_width),
                                                    1.0f / static_cast<float>(s_height), 0.0f));
    drawFullscreenInto(s_bright);

    // ===== 2/3. L0 ping-pong：水平 bright→blur0，垂直 blur0→bright（bright 内容已死，复用）=====
    runBlur(s_bright.tex, s_bright.w, s_bright.h, s_blur0, 1.0f, 0.0f);
    runBlur(s_blur0.tex, s_blur0.w, s_blur0.h, s_bright, 0.0f, 1.0f);

    // ===== 4/5. 降采样级（stride 2 单方向；前级已各向同性）=====
    const int levels = std::clamp(s.bloomLevels, 1, 3);
    if (levels >= 2)
        runBlur(s_bright.tex, s_bright.w, s_bright.h, s_blur1, 2.0f, 0.0f);
    if (levels >= 3)
        runBlur(s_blur1.tex, s_blur1.w, s_blur1.h, s_blur2, 2.0f, 0.0f);

    // uBloom = 末级（各级内容经链式降采样折叠其中；bloomLevels 控制扩散范围）
    const uint32_t bloomTex = (levels >= 3) ? s_blur2.tex : (levels == 2) ? s_blur1.tex : s_bright.tex;

    // ===== 6. combine：输出到调用方 FBO（应用入口时的绑定），视口恢复 =====
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<uint32_t>(prevDrawFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    s_combineShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColorTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex);
    s_combineShader->setInt("uScene", 0);
    s_combineShader->setInt("uBloom", 1);
    s_combineShader->setFloat("uBloomIntensity", std::max(s.bloomIntensity, 0.0f));
    s_combineShader->setFloat("uExposure", std::max(s.exposure, 0.001f));
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ===== 状态恢复 =====
    glBindVertexArray(static_cast<uint32_t>(prevVao));
    glUseProgram(static_cast<uint32_t>(prevProgram));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(prevTex0));
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<uint32_t>(prevDrawFbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<uint32_t>(prevReadFbo));
    if (prevDepthTest) glEnable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

}  // namespace NovaEngine
