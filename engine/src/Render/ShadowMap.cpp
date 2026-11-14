// ShadowMap.cpp —— 方向光阴影深度贴图（V0.2-6）
//
// 深度用 GL_DEPTH_COMPONENT24 纹理（非 depth renderbuffer）：后续主 pass 需要采样做 PCF。
// CLAMP_TO_BORDER + 白色边框：光空间盒外采样深度 = 1.0（最远）→ 盒外不受影。

#include <NovaEngine/Render/ShadowMap.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>   // 约定：先 glad 后 GLFW（GLFW_INCLUDE_NONE 已定义，顺序本身无冲突）
#include <GLFW/glfw3.h>

namespace NovaEngine {

bool ShadowMap::create(int size) {
    if (size <= 0) {
        Logger::error("[ShadowMap] 非法尺寸 {}", size);
        return false;
    }
    if (m_fbo != 0 && m_size == size)
        return true;   // 尺寸未变：复用现有 FBO（mapSize 变化时调用方会先触发重建）

    destroy();

    // 深度纹理（NEAREST、无 mipmap：PCF 手动比较用普通采样器）
    glGenTextures(1, &m_depthTex);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float kWhiteBorder[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, kWhiteBorder);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 深度专用 FBO：无颜色附件（glDrawBuffer(GL_NONE) 是完整性必要条件）
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        Logger::error("[ShadowMap] 深度 FBO 创建失败（{}x{}）", size, size);
        destroy();
        return false;
    }
    m_size = size;
    return true;
}

void ShadowMap::destroy() {
    if (m_depthTex) { glDeleteTextures(1, &m_depthTex); m_depthTex = 0; }
    if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
    m_size = 0;
}

void ShadowMap::bindForWriting() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_size, m_size);
}

void ShadowMap::bindForReading(uint32_t textureUnit) const {
    // 采样器参数（NEAREST/边框色）在 create 时已设置，这里只切纹理单元并绑定
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
}

uint32_t ShadowMap::depthTextureId() const { return m_depthTex; }
int ShadowMap::size() const { return m_size; }

}  // namespace NovaEngine
