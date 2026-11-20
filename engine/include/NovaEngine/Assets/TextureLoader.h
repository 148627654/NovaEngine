#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace NovaEngine {

// GPU 压缩能力（V1.0-6）：导入时查询一次（能力变化 → 缓存键变 → 自动重导）
struct GpuCompressionCaps {
    bool bc7 = false;       // GL_COMPRESSED_RGBA_BPTC_UNORM
    bool bc7Srgb = false;   // GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
    bool bc1 = false;       // GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
    bool bc1Srgb = false;   // GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
    bool bc5 = false;       // GL_COMPRESSED_RG_RGTC2
    // 指定格式（0=BC7 1=BC1 2=BC5）以 sRGB/线性内部格式是否可上传
    bool supports(int format, bool srgb) const;
    // 能力标志文本（进压缩缓存键，确定性输出）："bc7=1;bc7srgb=1;bc1=1;bc1srgb=1;bc5=1"
    std::string keyText() const;
};

class TextureLoader {
public:
    // 解码为 RGBA8（stb 不翻转：配合 Assimp FlipUVs，避免双翻转）；失败返回 false
    static bool loadRgba8(const std::filesystem::path& absolutePath,
                          int& outWidth, int& outHeight, std::vector<uint8_t>& outPixels);

    // ===== V1.0-6 压缩管线（bc7enc_rdo 离线编码；NOVA_HAS_BC7ENC 未定义 → encode 恒失败）=====
    // 查询当前 GL 上下文压缩能力（Renderer 未初始化 → 全 false；结果进程内缓存一次）
    static GpuCompressionCaps queryCompressionCaps();
    // 能力覆盖（验收「模拟不支持」注入；传 nullptr 恢复真实查询）
    static void setCompressionCapsOverride(const GpuCompressionCaps* caps);
    // RGBA8 → 压缩 mip 链（逐级 2x2 盒式降采样；4x4 块边缘钳制）；返回 mip 级数（<=0 失败）
    static int encodeCompressed(const uint8_t* rgba8, int w, int h, int format,
                                bool fullMipChain, std::vector<uint8_t>& outBlocks);
    // 格式块字节：BC7/BC5=16，BC1=8（非法格式 0）
    static size_t compressedBlockSize(int format);
    // 完整 mip 链级数（到 1x1 为止，>=1；非法尺寸 0）
    static int mipLevels(int w, int h);
    // mipCount 级链压缩后总字节（mipCount<=mipLevels(w,h)；参数非法 0）
    static size_t compressedSizeBytes(int format, int w, int h, int mipCount);
    // 检测有效 alpha（任一像素 a<255；Auto 策略 BC7/BC1 判定依据）
    static bool hasAlpha(const uint8_t* rgba8, size_t pixelCount);
};

}  // namespace NovaEngine
