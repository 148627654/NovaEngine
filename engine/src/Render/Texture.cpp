#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Render/Renderer.h>   // isInitialized（V1.0 波 2 合波 GL 守卫）
#include <NovaEngine/Assets/TextureLoader.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>

#include <algorithm>
#include <vector>

namespace NovaEngine {

namespace {

// 上传 RGBA8 像素为 2D 纹理（采样参数化，V0.3-5 .novameta 消费；默认参数等价旧行为）。
// 参数用引用回写纹理 id 与尺寸——冻结头不允许添加成员函数，工厂先 new 出 Texture 再经本函数填充
void uploadRgba8(uint32_t& outId, int& outWidth, int& outHeight, size_t& outBytes,
                 const uint8_t* pixels, int width, int height, bool srgb,
                 TextureWrap wrap, TextureFilter filter, bool generateMipmaps) {
    glGenTextures(1, &outId);
    glBindTexture(GL_TEXTURE_2D, outId);

    const GLint wrapMode = (wrap == TextureWrap::Repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
    // Trilinear 且生成 mipmap → 三线性；其余（Bilinear，或未生成 mipmap 防纹理不完整）→ 线性
    const GLint minFilter =
        (filter == TextureFilter::Trilinear && generateMipmaps) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // srgb 贴图用 sRGB 内部格式：采样自动线性化，配合片元着色器末尾的 gamma 校正输出
    const GLint internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (generateMipmaps)
        glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);
    outWidth = width;
    outHeight = height;
    outBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
}

// 上传压缩块数据（V1.0-6）：逐级 glCompressedTexImage2D（压缩纹理不可 glGenerateMipmap，
// mip 链由编码器离线生成）。mipCount 由调用方按 generateMipmaps 决定（1 或完整链）。
void uploadCompressed(uint32_t& outId, int& outWidth, int& outHeight, size_t& outBytes,
                      const uint8_t* blocks, int width, int height, int format, bool srgb,
                      TextureWrap wrap, TextureFilter filter, int mipCount) {
    glGenTextures(1, &outId);
    glBindTexture(GL_TEXTURE_2D, outId);

    const GLint wrapMode = (wrap == TextureWrap::Repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
    const GLint minFilter =
        (filter == TextureFilter::Trilinear && mipCount > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLenum internal;
    switch (format) {
        case 0:   // BC7（BPTC）
            internal = srgb ? GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM : GL_COMPRESSED_RGBA_BPTC_UNORM;
            break;
        case 1:   // BC1（S3TC DXT1）
            internal = srgb ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
                            : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            break;
        default:  // BC5（RGTC2；法线贴图 RG 两通道，无 sRGB 变体）
            internal = GL_COMPRESSED_RG_RGTC2;
            break;
    }
    const size_t blockSize = (format == 1) ? 8u : 16u;
    size_t used = 0;
    for (int i = 0; i < mipCount; ++i) {
        const int lw = std::max(1, width >> i), lh = std::max(1, height >> i);
        const size_t sz = static_cast<size_t>((lw + 3) / 4) * ((lh + 3) / 4) * blockSize;
        glCompressedTexImage2D(GL_TEXTURE_2D, i, internal, lw, lh, 0,
                               static_cast<GLsizei>(sz), blocks + used);
        used += sz;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    outWidth = width;
    outHeight = height;
    outBytes = used;
}

}  // namespace

std::shared_ptr<Texture> Texture::createWhite() {
    const uint8_t white[4] = {255, 255, 255, 255};
    auto tex = std::shared_ptr<Texture>(new Texture());
    uploadRgba8(tex->m_id, tex->m_width, tex->m_height, tex->m_sizeBytes, white, 1, 1, false,
                TextureWrap::Repeat, TextureFilter::Trilinear, true);
    return tex;
}

std::shared_ptr<Texture> Texture::createCheckerboard(int cells, glm::vec4 colorA, glm::vec4 colorB) {
    cells = std::max(1, cells);
    constexpr int kCellPx = 32;   // 每格像素数：避免小尺寸纹理被线性过滤糊成一片
    const int size = cells * kCellPx;

    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool a = ((x / kCellPx) + (y / kCellPx)) % 2 == 0;
            const glm::vec4 c = a ? colorA : colorB;
            const size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(c.r * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(c.g * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(c.b * 255.0f);
            pixels[idx + 3] = static_cast<uint8_t>(c.a * 255.0f);
        }
    }

    auto tex = std::shared_ptr<Texture>(new Texture());
    uploadRgba8(tex->m_id, tex->m_width, tex->m_height, tex->m_sizeBytes, pixels.data(), size,
                size, false, TextureWrap::Repeat, TextureFilter::Trilinear, true);
    return tex;
}

std::shared_ptr<Texture> Texture::createFromFile(const std::filesystem::path& path, bool srgb) {
    // 旧签名：转发默认采样参数（Repeat/Trilinear/生成 mipmap），行为与旧版逐字节等价
    return createFromFile(path, srgb, TextureWrap::Repeat, TextureFilter::Trilinear, true);
}

std::shared_ptr<Texture> Texture::createFromFile(const std::filesystem::path& path, bool srgb,
                                                 TextureWrap wrap, TextureFilter filter,
                                                 bool generateMipmaps) {
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;
    // 解码委托给 TextureLoader（stb_image 唯一实现点，翻转标志 false 与 Assimp FlipUVs 配套）
    if (!TextureLoader::loadRgba8(path, w, h, pixels)) {
        Logger::error("[Texture] 加载贴图失败: {}", path.string());
        return nullptr;
    }

    auto tex = std::shared_ptr<Texture>(new Texture());
    uploadRgba8(tex->m_id, tex->m_width, tex->m_height, tex->m_sizeBytes, pixels.data(), w, h,
                srgb, wrap, filter, generateMipmaps);
    return tex;
}

std::shared_ptr<Texture> Texture::createFromPixels(const uint8_t* rgba8, int width, int height,
                                                   bool srgb, TextureWrap wrap,
                                                   TextureFilter filter, bool generateMipmaps) {
    // ===== V1.0 波 2 合波（cR 遗留 3）：GL 就绪守卫 =====
    // 无 GL 上下文（headless 测试/未初始化）→ nullptr + warn，与 Mesh::create 同纪律；
    // 运行期必先 Renderer::init，行为零变化
    if (!Renderer::isInitialized()) {
        Logger::warn("[Texture] GL 未初始化，跳过贴图上传");
        return nullptr;
    }
    // 二进制缓存命中路径（V0.3-7）：像素已在内存，跳过文件读取与 stb 解码
    if (!rgba8 || width <= 0 || height <= 0) return nullptr;
    auto tex = std::shared_ptr<Texture>(new Texture());
    uploadRgba8(tex->m_id, tex->m_width, tex->m_height, tex->m_sizeBytes, rgba8, width, height,
                srgb, wrap, filter, generateMipmaps);
    return tex;
}

std::shared_ptr<Texture> Texture::createFromCompressed(const uint8_t* blocks, size_t blockBytes,
                                                       int width, int height, int format,
                                                       bool srgb, TextureWrap wrap,
                                                       TextureFilter filter,
                                                       bool generateMipmaps) {
    // 压缩块数据上传（V1.0-6）：块字节必须覆盖所上传的 mip 链
    if (!blocks || blockBytes == 0 || width <= 0 || height <= 0 || format < 0 || format > 2)
        return nullptr;
    const size_t blockSize = (format == 1) ? 8u : 16u;
    const int maxMips = TextureLoader::mipLevels(width, height);
    const int mipCount = generateMipmaps ? maxMips : 1;
    size_t needed = 0;
    for (int i = 0; i < mipCount; ++i) {
        const int lw = std::max(1, width >> i), lh = std::max(1, height >> i);
        needed += static_cast<size_t>((lw + 3) / 4) * ((lh + 3) / 4) * blockSize;
    }
    if (blockBytes < needed) {
        Logger::error("[Texture] 压缩块数据不足（{}x{} fmt {}：需 {} B，得 {} B）", width,
                      height, format, needed, blockBytes);
        return nullptr;
    }
    auto tex = std::shared_ptr<Texture>(new Texture());
    uploadCompressed(tex->m_id, tex->m_width, tex->m_height, tex->m_sizeBytes, blocks, width,
                     height, format, srgb, wrap, filter, mipCount);
    return tex;
}

Texture::~Texture() {
    if (m_id) glDeleteTextures(1, &m_id);
}

void Texture::bind(uint32_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

int Texture::width() const { return m_width; }
int Texture::height() const { return m_height; }
uint32_t Texture::handle() const { return m_id; }

size_t Texture::sizeBytes() const { return m_sizeBytes; }

}  // namespace NovaEngine
