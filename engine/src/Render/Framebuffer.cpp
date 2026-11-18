#include <NovaEngine/Render/Framebuffer.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>

namespace NovaEngine {

namespace {

// 构建渲染目标集合，返回主 FBO 是否完整。
// samples > 1：主 FBO 挂 MSAA 彩色纹理 + resolve FBO/纹理；否则 resolve 纹理直接挂主 FBO（resolveMsaa 变 no-op）。
// hdrColor=true（V0.3-14）：MSAA 颜色附件与 resolve 纹理均为 RGBA16F（MSAA blit 同格式）；
// 默认 RGBA8（旧行为逐像素一致）。
// normalDepth=true（V0.5-5 SSAO，§25.3）：多附件 MRT——附件 1 法线（RGBA16F）+ 附件 2 线性深度
// （RGBA16F），MSAA 时各建 MSAA 纹理并经 resolve FBO 逐附件 blit；无 MSAA 时 resolve 纹理直接挂主 FBO。
// 参数用引用回写句柄——冻结头不允许添加成员函数，create 经引用把成员变量交给本函数
bool buildAttachments(uint32_t& outFbo, uint32_t& outMsaaColor, uint32_t& outDepthStencil,
                      uint32_t& outResolveFbo, uint32_t& outResolveTex,
                      uint32_t& outMsaaNormal, uint32_t& outMsaaDepthLinear,
                      uint32_t& outResolveNormalTex, uint32_t& outResolveDepthTex,
                      int width, int height, int samples, bool hdrColor, bool normalDepth) {
    const bool msaa = samples > 1;
    const GLenum colorFormat = hdrColor ? GL_RGBA16F : GL_RGBA8;

    // MRT 附件 1/2 纹理构建辅助（V0.5-5 SSAO）：恒 RGBA16F（SSAO 输入格式，§25.3）；
    // MSAA 用 multisample 纹理，非 MSAA 用普通 2D（CLAMP 双线性）
    auto createMsaaTex = [&](uint32_t& out) {
        glGenTextures(1, &out);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, out);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA16F, width, height, GL_TRUE);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    };
    auto createTex = [&](uint32_t& out) {
        glGenTextures(1, &out);
        glBindTexture(GL_TEXTURE_2D, out);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    };

    glGenFramebuffers(1, &outFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, outFbo);

    if (msaa) {
        glGenTextures(1, &outMsaaColor);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, outMsaaColor);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, colorFormat, width, height, GL_TRUE);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D_MULTISAMPLE, outMsaaColor, 0);
    }

    // 深度+模板渲染缓冲（选中描边的 stencil 依赖模板位）
    glGenRenderbuffers(1, &outDepthStencil);
    glBindRenderbuffer(GL_RENDERBUFFER, outDepthStencil);
    if (msaa) {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
    } else {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, outDepthStencil);

    // ===== MRT 附件 1/2（V0.5-5 SSAO）：法线 + 线性深度——恒 RGBA16F（SSAO 输入格式，§25.3）=====
    if (normalDepth) {
        if (msaa) {
            createMsaaTex(outMsaaNormal);
            createMsaaTex(outMsaaDepthLinear);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                                   GL_TEXTURE_2D_MULTISAMPLE, outMsaaNormal, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                                   GL_TEXTURE_2D_MULTISAMPLE, outMsaaDepthLinear, 0);
        } else {
            // 无 MSAA：resolve 纹理直接挂主 FBO（与附件 0 同策略）
            createTex(outResolveNormalTex);
            createTex(outResolveDepthTex);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                                   GL_TEXTURE_2D, outResolveNormalTex, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                                   GL_TEXTURE_2D, outResolveDepthTex, 0);
        }
        const GLenum drawBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        glDrawBuffers(3, drawBuffers);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // resolve 纹理（ImGui::Image 显示用；HDR 时 RGBA16F，blit 同格式）
    glGenTextures(1, &outResolveTex);
    glBindTexture(GL_TEXTURE_2D, outResolveTex);
    if (hdrColor) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (msaa) {
        glGenFramebuffers(1, &outResolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, outResolveFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outResolveTex, 0);
        // resolve FBO 同样挂 MRT 附件 1/2（V0.5-5；无 MSAA 时 resolve 纹理已直挂主 FBO）
        if (normalDepth) {
            createTex(outResolveNormalTex);
            createTex(outResolveDepthTex);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, outResolveNormalTex, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, outResolveDepthTex, 0);
            const GLenum drawBuffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
            glDrawBuffers(3, drawBuffers);
        }
    } else {
        // 无 MSAA：当前绑定仍是 outFbo，resolve 纹理直接挂主 FBO
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outResolveTex, 0);
    }

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

}  // namespace

bool Framebuffer::create(int width, int height, int samples, bool hdrColor, bool normalDepth) {
    if (width <= 0 || height <= 0) {
        Logger::error("[Framebuffer] 非法尺寸 {}x{}", width, height);
        return false;
    }
    if (m_fbo != 0) destroy();

    m_width = width;
    m_height = height;
    m_samples = samples;
    m_hdrColor = hdrColor;
    m_normalDepth = normalDepth;

    if (buildAttachments(m_fbo, m_msaaColor, m_depthStencil, m_resolveFbo, m_resolveTex,
                         m_msaaNormal, m_msaaDepthLinear, m_resolveNormalTex, m_resolveDepthTex,
                         width, height, samples, hdrColor, normalDepth)) {
        return true;
    }

    // 完整检查失败：全删重建，降级 samples=0 无 MSAA 路径（集成显卡安全网，ARCHITECTURE.md §11-12）
    Logger::error("[Framebuffer] FBO 创建失败（{}x{} MSAA {}x），降级为无 MSAA 路径", width, height, samples);
    destroy();
    m_width = width;
    m_height = height;
    m_samples = 0;
    m_hdrColor = hdrColor;
    m_normalDepth = normalDepth;

    if (buildAttachments(m_fbo, m_msaaColor, m_depthStencil, m_resolveFbo, m_resolveTex,
                         m_msaaNormal, m_msaaDepthLinear, m_resolveNormalTex, m_resolveDepthTex,
                         width, height, 0, hdrColor, normalDepth)) {
        return true;
    }

    Logger::error("[Framebuffer] 无 MSAA 降级路径同样失败（{}x{}）", width, height);
    destroy();
    return false;
}

void Framebuffer::destroy() {
    if (m_resolveDepthTex) { glDeleteTextures(1, &m_resolveDepthTex); m_resolveDepthTex = 0; }
    if (m_resolveNormalTex) { glDeleteTextures(1, &m_resolveNormalTex); m_resolveNormalTex = 0; }
    if (m_msaaDepthLinear) { glDeleteTextures(1, &m_msaaDepthLinear); m_msaaDepthLinear = 0; }
    if (m_msaaNormal) { glDeleteTextures(1, &m_msaaNormal); m_msaaNormal = 0; }
    if (m_resolveTex) { glDeleteTextures(1, &m_resolveTex); m_resolveTex = 0; }
    if (m_msaaColor) { glDeleteTextures(1, &m_msaaColor); m_msaaColor = 0; }
    if (m_depthStencil) { glDeleteRenderbuffers(1, &m_depthStencil); m_depthStencil = 0; }
    if (m_resolveFbo) { glDeleteFramebuffers(1, &m_resolveFbo); m_resolveFbo = 0; }
    if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
    m_width = 0;
    m_height = 0;
}

void Framebuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;   // 尺寸不变：no-op
    const int samples = m_samples;
    const bool hdr = m_hdrColor;
    const bool normalDepth = m_normalDepth;   // V0.5-5：MRT 标志保持重建
    destroy();
    if (width > 0 && height > 0) {
        create(width, height, samples, hdr, normalDepth);   // 格式保持重建（V0.3-14）
    }
    // 尺寸为 0（窗口最小化/面板折叠）：保持空状态，调用方应跳过本 FBO 的渲染
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::resolveMsaa() const {
    if (m_samples <= 0 || m_resolveFbo == 0 || m_fbo == 0) return;   // 无 MSAA 无需 resolve
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    // MRT 多附件 resolve（V0.5-5 SSAO）：逐附件读缓冲 + 同尺寸 blit（GL_NEAREST 与颜色一致）
    if (m_normalDepth) {
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glBlitFramebuffer(0, 0, m_width, m_height,
                          0, 0, m_width, m_height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glReadBuffer(GL_COLOR_ATTACHMENT2);
        glBlitFramebuffer(0, 0, m_width, m_height,
                          0, 0, m_width, m_height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glReadBuffer(GL_COLOR_ATTACHMENT0);   // 恢复默认读缓冲（全局 GL 状态）
    }
    // 恢复调用方状态（文档约定 resolveMsaa 在 bind 状态下调用）
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}

uint32_t Framebuffer::colorTextureId() const { return m_resolveTex; }
uint32_t Framebuffer::normalTextureId() const { return m_resolveNormalTex; }
uint32_t Framebuffer::depthLinearTextureId() const { return m_resolveDepthTex; }
int Framebuffer::width() const { return m_width; }
int Framebuffer::height() const { return m_height; }

Framebuffer::~Framebuffer() { destroy(); }

}  // namespace NovaEngine
