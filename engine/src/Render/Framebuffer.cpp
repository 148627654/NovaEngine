#include <NovaEngine/Render/Framebuffer.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>

namespace NovaEngine {

namespace {

// 构建渲染目标集合，返回主 FBO 是否完整。
// samples > 1：主 FBO 挂 MSAA 彩色纹理 + resolve FBO/纹理；否则 resolve 纹理直接挂主 FBO（resolveMsaa 变 no-op）。
// 参数用引用回写句柄——冻结头不允许添加成员函数，create 经引用把成员变量交给本函数
bool buildAttachments(uint32_t& outFbo, uint32_t& outMsaaColor, uint32_t& outDepthStencil,
                      uint32_t& outResolveFbo, uint32_t& outResolveTex,
                      int width, int height, int samples) {
    const bool msaa = samples > 1;

    glGenFramebuffers(1, &outFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, outFbo);

    if (msaa) {
        glGenTextures(1, &outMsaaColor);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, outMsaaColor);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA8, width, height, GL_TRUE);
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

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // resolve 纹理（ImGui::Image 显示用）
    glGenTextures(1, &outResolveTex);
    glBindTexture(GL_TEXTURE_2D, outResolveTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (msaa) {
        glGenFramebuffers(1, &outResolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, outResolveFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outResolveTex, 0);
    } else {
        // 无 MSAA：当前绑定仍是 outFbo，resolve 纹理直接挂主 FBO
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outResolveTex, 0);
    }

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

}  // namespace

bool Framebuffer::create(int width, int height, int samples) {
    if (width <= 0 || height <= 0) {
        Logger::error("[Framebuffer] 非法尺寸 {}x{}", width, height);
        return false;
    }
    if (m_fbo != 0) destroy();

    m_width = width;
    m_height = height;
    m_samples = samples;

    if (buildAttachments(m_fbo, m_msaaColor, m_depthStencil, m_resolveFbo, m_resolveTex,
                         width, height, samples)) {
        return true;
    }

    // 完整检查失败：全删重建，降级 samples=0 无 MSAA 路径（集成显卡安全网，ARCHITECTURE.md §11-12）
    Logger::error("[Framebuffer] FBO 创建失败（{}x{} MSAA {}x），降级为无 MSAA 路径", width, height, samples);
    destroy();
    m_width = width;
    m_height = height;
    m_samples = 0;

    if (buildAttachments(m_fbo, m_msaaColor, m_depthStencil, m_resolveFbo, m_resolveTex,
                         width, height, 0)) {
        return true;
    }

    Logger::error("[Framebuffer] 无 MSAA 降级路径同样失败（{}x{}）", width, height);
    destroy();
    return false;
}

void Framebuffer::destroy() {
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
    destroy();
    if (width > 0 && height > 0) {
        create(width, height, samples);
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
    // 恢复调用方状态（文档约定 resolveMsaa 在 bind 状态下调用）
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}

uint32_t Framebuffer::colorTextureId() const { return m_resolveTex; }
int Framebuffer::width() const { return m_width; }
int Framebuffer::height() const { return m_height; }

Framebuffer::~Framebuffer() { destroy(); }

}  // namespace NovaEngine
