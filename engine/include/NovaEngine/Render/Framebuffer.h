#pragma once
#include <cstdint>

namespace NovaEngine {

class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    bool create(int width, int height, int samples = 4, bool hdrColor = false);
    // hdrColor=true（V0.3-14 波 3）：RGBA16F 颜色附件 + 16F resolve 纹理（MSAA blit 同格式）；
    // 旧调用不变（默认 RGBA8，逐像素兼容）
    void destroy();
    void resize(int width, int height);                    // 尺寸不变则 no-op；全删重建（简单可靠）
    void bind() const;                                     // 绑定并 glViewport(0,0,w,h)
    void unbind() const;                                   // 恢复默认 FBO
    void resolveMsaa() const;                              // glBlitFramebuffer 到 resolve 纹理（bind 状态下调用）
    uint32_t colorTextureId() const;                       // 供 ImGui::Image 使用
    uint32_t fboId() const { return m_fbo; }               // 诊断读回/外部绑定（验收诊断期使用）
    int width() const;
    int height() const;

private:
    uint32_t m_fbo = 0, m_msaaColor = 0, m_depthStencil = 0, m_resolveFbo = 0, m_resolveTex = 0;
    int m_width = 0, m_height = 0, m_samples = 4;
    bool m_hdrColor = false;
};

}  // namespace NovaEngine
