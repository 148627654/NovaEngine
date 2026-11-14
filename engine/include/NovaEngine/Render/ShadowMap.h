#pragma once
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace NovaEngine {

// 阴影深度 pass 参数（编辑器粘合层从 DirectionalLight + 渲染相机收集后传入）
struct ShadowCameraParams {
    glm::vec3 lightDirection = glm::normalize(glm::vec3(0.5f, -1.0f, -0.3f));  // 指向光源
    glm::mat4 lightViewProj{1.0f};   // 光空间矩阵（已纹素对齐）
    int mapSize = 2048;              // 深度贴图边长（px）
    float bias = 0.0005f;            // 深度偏置（世界单位）
};

// 光空间正交矩阵（V0.2-6）：固定范围 + 跟随相机中心（沿光轴投影），纹素对齐防蠕动
inline glm::mat4 computeLightViewProj(const glm::vec3& lightDir, const glm::vec3& center,
                                      float range, int mapSize) {
    const float extendZ = range * 2.0f;
    const glm::vec3 up = std::abs(lightDir.y) > 0.99f
                             ? glm::vec3(1.0f, 0.0f, 0.0f)   // 光接近竖直时换参考轴
                             : glm::vec3(0.0f, 1.0f, 0.0f);
    // 光相机放在光源侧、沿光传播方向（-lightDir）看向场景中心
    const glm::vec3 eye = center + lightDir * extendZ;
    const glm::mat4 lightView = glm::lookAt(eye, center, up);
    const float nearZ = std::max(extendZ - range - 1.0f, 0.1f);
    const glm::mat4 lightProj =
        glm::ortho(-range, range, -range, range, nearZ, extendZ + range + 1.0f);
    glm::mat4 vp = lightProj * lightView;
    // 纹素对齐：世界原点在光空间中的位置对齐到纹素网格（防阴影蠕动）
    glm::vec4 originLS = vp * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    originLS /= originLS.w;
    const float halfN = static_cast<float>(mapSize) * 0.5f;
    const glm::vec2 texCoord = glm::vec2(originLS) * halfN;
    const glm::vec2 offset = (glm::round(texCoord) - texCoord) / halfN;
    vp = glm::translate(glm::mat4(1.0f), glm::vec3(offset, 0.0f)) * vp;
    return vp;
}

// 深度 FBO（GL_DEPTH_COMPONENT24；CLAMP_TO_BORDER + 白色边框 = 阴影盒外不受影）
class ShadowMap {
public:
    bool create(int size);                            // 失败返回 false + Logger::error
    void destroy();
    void bindForWriting() const;                      // 绑 FBO + glViewport(0,0,size,size)
    void bindForReading(uint32_t textureUnit) const;  // glActiveTexture + 绑深度纹理
    uint32_t depthTextureId() const;
    int size() const;

private:
    uint32_t m_fbo = 0, m_depthTex = 0;
    int m_size = 0;
};

}  // namespace NovaEngine
