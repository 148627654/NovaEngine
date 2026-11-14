// Cubemap.cpp —— 立方体贴图创建（V0.2-7）
//
// 两条创建路径：
//   1. createFromFaces：6 面图（顺序 +X, -X, +Y, -Y, +Z, -Z）。LDR 走 TextureLoader::loadRgba8
//      （srgb=true 时内部格式 GL_SRGB8_ALPHA8，采样自动线性化）；.hdr 走 stbi_loadf → GL_RGB16F。
//   2. createFromEquirect：等距柱状 2D 图 → 临时 FBO（GL_RGB16F）+ equirect_to_cube shader
//      逐面渲染 + glCopyTexSubImage2D 拷入 cubemap。
// 内部一律线性存储；输出前 gamma 由消费端着色器（skybox.frag）统一处理。

#include <NovaEngine/Render/Cubemap.h>
#include <NovaEngine/Render/Primitives.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Assets/TextureLoader.h>
#include <NovaEngine/Core/Logger.h>

#include <stb_image.h>   // 仅声明：STB_IMAGE_IMPLEMENTATION 唯一实现点在 TextureLoader.cpp

#include <glad/gl.h>   // 约定：先 glad 后 GLFW
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>   // lookAt / perspective / radians

namespace NovaEngine {

namespace {

// 扩展名 → 是否 HDR（.hdr 用 stbi_loadf 浮点加载，其余走 TextureLoader::loadRgba8）
bool isHdrPath(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".hdr";
}

// 通用 cubemap 采样参数（线性 + mipmap + clamp）
void setCommonParams() {
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

}  // namespace

Cubemap::~Cubemap() {
    if (m_id) glDeleteTextures(1, &m_id);
}

std::shared_ptr<Cubemap> Cubemap::createFromFaces(
    const std::array<std::filesystem::path, 6>& facePaths, bool srgb) {
    // 全部 .hdr → HDR 路径（RGB16F）；全部非 .hdr → LDR；混合 → 失败（单纹理无法混用内部格式）
    const bool hdr = isHdrPath(facePaths[0]);
    for (int i = 1; i < 6; ++i) {
        if (isHdrPath(facePaths[i]) != hdr) {
            Logger::error("[Cubemap] 6 面图 HDR/LDR 混用，无法创建（单纹理内部格式须一致）");
            return nullptr;
        }
    }

    auto cubemap = std::shared_ptr<Cubemap>(new Cubemap());
    glGenTextures(1, &cubemap->m_id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap->m_id);
    setCommonParams();

    int faceSize = 0;
    for (int i = 0; i < 6; ++i) {
        int w = 0, h = 0;
        if (hdr) {
            // HDR：stb 直接解码为 float RGB；翻转设置与 TextureLoader 一致（不翻转）
            stbi_set_flip_vertically_on_load(0);
            int channels = 0;
            float* data = stbi_loadf(facePaths[i].string().c_str(), &w, &h, &channels, 3);
            if (!data) {
                Logger::error("[Cubemap] 解码 HDR 面失败 {}: {}", facePaths[i].string(),
                              stbi_failure_reason());
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                glDeleteTextures(1, &cubemap->m_id);
                cubemap->m_id = 0;   // 已手动释放：置 0 防析构函数重复删除
                return nullptr;
            }
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, w, h, 0,
                         GL_RGB, GL_FLOAT, data);
            stbi_image_free(data);
        } else {
            std::vector<uint8_t> pixels;
            if (!TextureLoader::loadRgba8(facePaths[i], w, h, pixels)) {
                Logger::error("[Cubemap] 解码 LDR 面失败 {}", facePaths[i].string());
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                glDeleteTextures(1, &cubemap->m_id);
                cubemap->m_id = 0;   // 已手动释放：置 0 防析构函数重复删除
                return nullptr;
            }
            // srgb=true 时用 sRGB 内部格式：采样自动线性化，消费端输出前统一 gamma 校正
            const GLint internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internal, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        if (i == 0) {
            faceSize = w;
        } else if (w != faceSize || h != faceSize) {
            Logger::error("[Cubemap] 6 面尺寸不一致（{}x{} != {}x{}），无法生成 mipmap",
                          w, h, faceSize, faceSize);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glDeleteTextures(1, &cubemap->m_id);
            cubemap->m_id = 0;   // 已手动释放：置 0 防析构函数重复删除
            return nullptr;
        }
    }
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    cubemap->m_faceSize = faceSize;
    return cubemap;
}

std::shared_ptr<Cubemap> Cubemap::createFromEquirect(const std::filesystem::path& absolutePath,
                                                     int faceSize) {
    if (faceSize <= 0) {
        Logger::error("[Cubemap] 非法面尺寸 {}", faceSize);
        return nullptr;
    }
    const bool hdr = isHdrPath(absolutePath);

    // 1. 加载等距柱状 2D 源贴图（LDR：sRGB 内部格式采样自动线性化；HDR：RGB16F）
    uint32_t srcTex = 0;
    int srcW = 0, srcH = 0;
    glGenTextures(1, &srcTex);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (hdr) {
        stbi_set_flip_vertically_on_load(0);   // 与 TextureLoader 一致：不翻转
        int channels = 0;
        float* data = stbi_loadf(absolutePath.string().c_str(), &srcW, &srcH, &channels, 3);
        if (!data) {
            Logger::error("[Cubemap] 解码 HDR 失败 {}: {}", absolutePath.string(),
                          stbi_failure_reason());
            glDeleteTextures(1, &srcTex);
            return nullptr;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, srcW, srcH, 0, GL_RGB, GL_FLOAT, data);
        stbi_image_free(data);
    } else {
        std::vector<uint8_t> pixels;
        if (!TextureLoader::loadRgba8(absolutePath, srcW, srcH, pixels)) {
            Logger::error("[Cubemap] 解码 LDR 失败 {}", absolutePath.string());
            glDeleteTextures(1, &srcTex);
            return nullptr;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, srcW, srcH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // 2. equirect → cubemap 转换着色器（失败：清理后返回 nullptr）
    auto shader = Shader::createFromFiles("assets/shaders/equirect_to_cube.vert",
                                          "assets/shaders/equirect_to_cube.frag");
    if (!shader) {
        glDeleteTextures(1, &srcTex);
        return nullptr;
    }

    // 3. 目标 cubemap（6 面 RGB16F 空分配，稍后 glCopyTexSubImage2D 填充）
    auto cubemap = std::shared_ptr<Cubemap>(new Cubemap());
    glGenTextures(1, &cubemap->m_id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap->m_id);
    setCommonParams();
    for (int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, faceSize, faceSize, 0,
                     GL_RGB, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    cubemap->m_faceSize = faceSize;

    // 4. 无 MSAA 临时 FBO（GL_RGB16F 颜色 + GL_DEPTH_COMPONENT24 深度，faceSize²）
    //    深度附件必需：立方体近面必须遮挡远面（Reviewer P1-1），否则 6 面内容互相覆盖污染
    uint32_t tmpFbo = 0, tmpRbo = 0, tmpDepthRbo = 0;
    glGenFramebuffers(1, &tmpFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, tmpFbo);
    glGenRenderbuffers(1, &tmpRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, tmpRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB16F, faceSize, faceSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, tmpRbo);
    glGenRenderbuffers(1, &tmpDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, tmpDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize, faceSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, tmpDepthRbo);
    const bool fboOk = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!fboOk) {
        Logger::error("[Cubemap] 转换 FBO 创建失败（{}x{} RGB16F）", faceSize, faceSize);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteRenderbuffers(1, &tmpRbo);
        glDeleteRenderbuffers(1, &tmpDepthRbo);
        glDeleteFramebuffers(1, &tmpFbo);
        glDeleteTextures(1, &srcTex);
        glDeleteTextures(1, &cubemap->m_id);
        cubemap->m_id = 0;   // 已手动释放：置 0 防析构函数重复删除
        return nullptr;
    }

    // 5. 保存 GL 状态（转换完恢复，避免干扰调用方渲染状态）
    GLint prevFbo = 0, prevProgram = 0, prevVao = 0, prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    auto cube = Primitives::cube();
    if (!cube) {
        Logger::error("[Cubemap] 转换用单位立方体创建失败");
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glDeleteRenderbuffers(1, &tmpRbo);
        glDeleteRenderbuffers(1, &tmpDepthRbo);
        glDeleteFramebuffers(1, &tmpFbo);
        glDeleteTextures(1, &srcTex);
        glDeleteTextures(1, &cubemap->m_id);
        cubemap->m_id = 0;   // 已手动释放：置 0 防析构函数重复删除
        return nullptr;
    }

    glViewport(0, 0, faceSize, faceSize);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);   // 相机在立方体中心：可见的是各面的背面，剔除会画空
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    shader->bind();
    shader->setInt("uEquirectMap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);

    // 6 面方向/up 组合（GL cubemap face 约定）
    const glm::vec3 kFaceDirs[6] = {
        glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec3( 0.0f,-1.0f, 0.0f),
        glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec3( 0.0f, 0.0f,-1.0f),
    };
    const glm::vec3 kFaceUps[6] = {
        glm::vec3(0.0f,-1.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f,-1.0f),
        glm::vec3(0.0f,-1.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f),
    };
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

    for (int i = 0; i < 6; ++i) {
        // 相机在原点，看向该面方向（单位立方体恰好铺满 90° 视锥）
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), kFaceDirs[i], kFaceUps[i]);
        shader->setMat4("uView", view);
        shader->setMat4("uProj", proj);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   // 边缘防缝隙：每面先清黑
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        cube->draw();

        // 从当前 FBO 拷贝进对应面（mip 0）
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap->m_id);
        glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, 0, 0, 0, 0,
                            faceSize, faceSize);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap->m_id);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    // 6. 恢复状态 + 清理临时资源
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<uint32_t>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(static_cast<uint32_t>(prevProgram));
    glBindVertexArray(static_cast<uint32_t>(prevVao));
    glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDeleteRenderbuffers(1, &tmpRbo);
    glDeleteRenderbuffers(1, &tmpDepthRbo);
    glDeleteFramebuffers(1, &tmpFbo);
    glDeleteTextures(1, &srcTex);
    return cubemap;
}

void Cubemap::bind(uint32_t unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);
}

uint32_t Cubemap::handle() const { return m_id; }
int Cubemap::faceSize() const { return m_faceSize; }

}  // namespace NovaEngine
