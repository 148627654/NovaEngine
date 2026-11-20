// TextureLoader.cpp —— stb_image 解码实现
//
// 注意：STB_IMAGE_IMPLEMENTATION 必须且只能在本文件定义一次（全项目唯一实现点），
// 多处定义会导致 LNK2005 重复符号错误。

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/TextureLoader.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Renderer.h>   // V1.0-6：isInitialized（GL 就绪守卫，assets→render 合法方向）

#include <glad/gl.h>

#include <format>

#include "TextureCompressImpl.h"   // V1.0-6：bc7enc_rdo 编码实现（内部头；NOVA_HAS_BC7ENC 守卫）

namespace NovaEngine {

namespace {

// 压缩能力缓存（进程内查一次；GPU 能力不变，变化场景由缓存键的 caps 文本跨进程兜底）
GpuCompressionCaps s_capsCached;
bool s_capsQueried = false;
// 能力覆盖（验收「模拟不支持」注入；nullptr = 真实查询）
const GpuCompressionCaps* s_capsOverride = nullptr;

GpuCompressionCaps queryCompressionCapsOnce() {
    GpuCompressionCaps caps;
    // 无 GL 上下文（NovaTests 等无窗口进程）→ 全 false → 上层回退未压缩路径
    if (!Renderer::isInitialized()) return caps;
    // 逐格式权威查询：glGetInternalformativ(GL_INTERNALFORMAT_SUPPORTED)。
    // 注：契约文本中的 GL_TEXTURE_COMPRESSED 是「当前绑定纹理是否已压缩」的对象状态
    // （首张贴图导入时恒为 false），不可作能力门——以 glGetInternalformativ 为准。
    const auto supported = [](GLenum fmt) {
        GLint v = 0;
        glGetInternalformativ(GL_TEXTURE_2D, fmt, GL_INTERNALFORMAT_SUPPORTED, 1, &v);
        return v != 0;
    };
    caps.bc7 = supported(GL_COMPRESSED_RGBA_BPTC_UNORM);
    caps.bc7Srgb = supported(GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM);
    caps.bc1 = supported(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
    caps.bc1Srgb = supported(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT);
    caps.bc5 = supported(GL_COMPRESSED_RG_RGTC2);
    return caps;
}

}  // namespace

bool TextureLoader::loadRgba8(const std::filesystem::path& absolutePath,
                              int& outWidth, int& outHeight, std::vector<uint8_t>& outPixels) {
    // 文件读取收敛到 AssetStream（裁定 1：内存解码 stbi_load_from_memory，不再 fopen；
    // ifstream path 重载中文路径安全，同时服务 Wave 2 的 pak 内存 entry 表）
    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(absolutePath, bytes)) {
        Logger::error("TextureLoader: 读取文件失败 {}", absolutePath.string());
        return false;
    }

    // 不翻转（flip = false）：与 Assimp 的 aiProcess_FlipUVs 配套，
    // 避免「双翻转」经典 bug（贴图上下颠倒）。
    stbi_set_flip_vertically_on_load(0);

    int width = 0, height = 0, channels = 0;
    // 强制解码为 RGBA8（desired_channels = 4），与渲染端纹理格式一致
    unsigned char* data =
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                              &width, &height, &channels, 4);
    if (!data) {
        Logger::error("TextureLoader: 解码失败 {}: {}", absolutePath.string(), stbi_failure_reason());
        return false;
    }

    outWidth = width;
    outHeight = height;
    outPixels.assign(data, data + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    stbi_image_free(data);
    return true;
}

// ---- V1.0-6 压缩管线 ----

bool GpuCompressionCaps::supports(int format, bool srgb) const {
    switch (format) {
        case 0: return srgb ? bc7Srgb : bc7;
        case 1: return srgb ? bc1Srgb : bc1;
        case 2: return bc5;   // BC5 无 sRGB 变体（法线贴图按线性）
        default: return false;
    }
}

std::string GpuCompressionCaps::keyText() const {
    return std::format("bc7={};bc7srgb={};bc1={};bc1srgb={};bc5={}", bc7 ? 1 : 0,
                       bc7Srgb ? 1 : 0, bc1 ? 1 : 0, bc1Srgb ? 1 : 0, bc5 ? 1 : 0);
}

GpuCompressionCaps TextureLoader::queryCompressionCaps() {
    if (s_capsOverride) return *s_capsOverride;
    if (!s_capsQueried) {
        s_capsCached = queryCompressionCapsOnce();
        s_capsQueried = true;
    }
    return s_capsCached;
}

void TextureLoader::setCompressionCapsOverride(const GpuCompressionCaps* caps) {
    s_capsOverride = caps;
}

int TextureLoader::encodeCompressed(const uint8_t* rgba8, int w, int h, int format,
                                    bool fullMipChain, std::vector<uint8_t>& outBlocks) {
    if (!rgba8 || w <= 0 || h <= 0 || format < 0 || format > 2) {
        outBlocks.clear();
        return 0;
    }
    return detail::encodeCompressedImpl(rgba8, w, h, format, fullMipChain, outBlocks);
}

size_t TextureLoader::compressedBlockSize(int format) {
    return detail::blockBytesFor(format);
}

int TextureLoader::mipLevels(int w, int h) {
    return detail::mipLevelsFor(w, h);
}

size_t TextureLoader::compressedSizeBytes(int format, int w, int h, int mipCount) {
    return detail::compressedBytesImpl(format, w, h, mipCount);
}

bool TextureLoader::hasAlpha(const uint8_t* rgba8, size_t pixelCount) {
    if (!rgba8) return false;
    for (size_t i = 0; i < pixelCount; ++i)
        if (rgba8[i * 4 + 3] != 255) return true;
    return false;
}

}  // namespace NovaEngine
