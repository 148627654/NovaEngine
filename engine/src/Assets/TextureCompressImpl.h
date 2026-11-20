// TextureCompressImpl.h —— V1.0-6 bc7enc_rdo 离线压缩编码实现（引擎内部头，不安装）
//
// NOVA_HAS_BC7ENC 由 cT 在 cmake/Dependencies.cmake 接入 bc7enc_rdo 静态库后经
// target_compile_definitions 定义（§26.0 第三方库裁定 2：仅编 bc7enc.cpp + rgbcx.cpp）。
// 未定义（占位期）→ 编码恒失败返回 0，上层回退未压缩 RGBA8 路径（warn 一次）。
//
// 本头不依赖 GL / Logger / stb / 文件系统（纯 CPU），TextureLoader.cpp 与
// 本地自测程序（build/cG/enc_test）共用同一份编码逻辑，避免自测与生产代码漂移。
//
// 编码约定：
//   - format 0=BC7 1=BC1 2=BC5（与 BinaryCache 压缩载荷一致）
//   - BC7 高质模式 = perceptual weights（§26.3「BC7 高质模式」验收）
//   - BC1 = rgbcx 级别 API level 10（allow_3color=true；无 punchthrough alpha——
//     Auto 策略只在无 alpha 时选 BC1，4 色模式即可）
//   - BC5 = rgbcx::encode_bc5_hq（法线贴图 RG 两通道，高质量模式）
//   - mip 链：2x2 盒式降采样逐级生成（压缩纹理不可 glGenerateMipmap，§26.3）
//   - 尺寸非 4 的倍数：块边缘像素钳制补足（标准做法）

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef NOVA_HAS_BC7ENC
#include <bc7enc.h>
#include <rgbcx.h>
#endif

namespace NovaEngine {
namespace detail {

// BC1 编码质量级别（rgbcx 0-18；10 为「高于 libsquish 均值」档，离线导入可接受）
constexpr uint32_t kBc1EncodeLevel = 10;

// 块字节（与 TextureLoader::compressedBlockSize 一致）
inline size_t blockBytesFor(int format) { return (format == 1) ? 8u : 16u; }

// mipCount 级链总字节（<=mipLevelsFor 时有效）
inline size_t compressedBytesImpl(int format, int w, int h, int mipCount) {
    if (format < 0 || format > 2 || w <= 0 || h <= 0 || mipCount <= 0) return 0;
    const size_t bs = blockBytesFor(format);
    size_t total = 0;
    for (int i = 0; i < mipCount; ++i) {
        const int lw = std::max(1, w >> i), lh = std::max(1, h >> i);
        total += static_cast<size_t>((lw + 3) / 4) * ((lh + 3) / 4) * bs;
    }
    return total;
}

// 完整 mip 链级数（到 1x1；>=1）
inline int mipLevelsFor(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    int levels = 1;
    while (w > 1 || h > 1) {
        ++levels;
        if (w > 1) w >>= 1;
        if (h > 1) h >>= 1;
    }
    return levels;
}

// 4x4 块像素收集（越界钳制到边缘像素）
inline void gatherBlock(const uint8_t* img, int w, int h, int bx, int by, uint8_t out[64]) {
    for (int y = 0; y < 4; ++y) {
        const int sy = std::min(by * 4 + y, h - 1);
        for (int x = 0; x < 4; ++x) {
            const int sx = std::min(bx * 4 + x, w - 1);
            std::memcpy(out + static_cast<size_t>(y * 4 + x) * 4,
                        img + (static_cast<size_t>(sy) * w + sx) * 4, 4);
        }
    }
}

// 2x2 盒式降采样（w/2 向下取整，最小 1x1；alpha 一并平均）
inline void downsampleBox(const std::vector<uint8_t>& src, int w, int h,
                          std::vector<uint8_t>& dst) {
    const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
    dst.assign(static_cast<size_t>(nw) * nh * 4, 0);
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            int acc[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::min(w - 1, x * 2 + dx);
                    const int sy = std::min(h - 1, y * 2 + dy);
                    const uint8_t* p = src.data() + (static_cast<size_t>(sy) * w + sx) * 4;
                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
                }
            }
            uint8_t* d = dst.data() + (static_cast<size_t>(y) * nw + x) * 4;
            for (int c = 0; c < 4; ++c) d[c] = static_cast<uint8_t>((acc[c] + 2) / 4);
        }
    }
}

#ifdef NOVA_HAS_BC7ENC

// 编码器一次性初始化（bc7enc 块参数初始化 + rgbcx 全局 init；单线程导入路径无竞争）
inline void ensureEncoderInit() {
    static const bool s_init = [] {
        bc7enc_compress_block_init();
        rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
        return true;
    }();
    (void)s_init;
}

// 单级编码：bw×bh 块 → dst（调用方保证 dst 长度 = bw*bh*块字节）
inline void encodeLevel(const uint8_t* img, int w, int h, int format, int bw, int bh,
                        size_t blockSize, uint8_t* dst,
                        const bc7enc_compress_block_params* bc7Params) {
    uint8_t block[64];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            gatherBlock(img, w, h, bx, by, block);
            uint8_t* d = dst + (static_cast<size_t>(by) * bw + bx) * blockSize;
            if (format == 0) {
                bc7enc_compress_block(d, block, bc7Params);
            } else if (format == 1) {
                rgbcx::encode_bc1(kBc1EncodeLevel, d, block, /*allow_3color=*/true,
                                  /*use_transparent_texels_for_black=*/false);
            } else {
                rgbcx::encode_bc5_hq(d, block, /*chan0=*/0, /*chan1=*/1);
            }
        }
    }
}

// RGBA8 → 压缩 mip 链。返回 mip 级数；参数非法/编码失败 <=0
inline int encodeCompressedImpl(const uint8_t* rgba8, int w, int h, int format,
                                bool fullMipChain, std::vector<uint8_t>& outBlocks) {
    if (!rgba8 || w <= 0 || h <= 0 || format < 0 || format > 2) return 0;
    ensureEncoderInit();
    const size_t bs = (format == 1) ? 8u : 16u;
    bc7enc_compress_block_params bc7Params;
    // BC7 高质模式：全字段初始化（全模式 64 分区 + 最小二乘 + perceptual 权重）。
    // 注意不能只调 init_perceptual_weights——它只设 m_perceptual/m_weights，
    // 其余字段（mode_mask/max_partitions 等）保持未初始化 → 崩溃（自测已踩坑）。
    bc7enc_compress_block_params_init(&bc7Params);

    const int maxMips = fullMipChain ? mipLevelsFor(w, h) : 1;
    std::vector<uint8_t> level(rgba8, rgba8 + static_cast<size_t>(w) * h * 4);
    std::vector<uint8_t> next;
    outBlocks.clear();
    outBlocks.reserve(compressedBytesImpl(format, w, h, maxMips));

    for (int mip = 0; mip < maxMips; ++mip) {
        const int lw = std::max(1, w >> mip), lh = std::max(1, h >> mip);
        const int bw = (lw + 3) / 4, bh = (lh + 3) / 4;
        const size_t base = outBlocks.size();
        outBlocks.resize(base + static_cast<size_t>(bw) * bh * bs);
        encodeLevel(level.data(), lw, lh, format, bw, bh, bs, outBlocks.data() + base,
                    &bc7Params);
        if (mip + 1 < maxMips) {
            downsampleBox(level, lw, lh, next);
            level.swap(next);
        }
    }
    return maxMips;
}

#else   // NOVA_HAS_BC7ENC 未定义：占位期恒失败（回退未压缩路径，warn 由上层负责）

inline int encodeCompressedImpl(const uint8_t*, int, int, int, bool, std::vector<uint8_t>&) {
    return 0;
}

#endif

}  // namespace detail
}  // namespace NovaEngine
