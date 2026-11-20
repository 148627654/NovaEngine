#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <glm/vec4.hpp>

namespace NovaEngine {

// 采样参数（V0.3-5 .novameta 消费；定义在 render 侧供 assets 模块引用，避免反向依赖）
enum class TextureWrap { Repeat, ClampToEdge };
enum class TextureFilter { Bilinear, Trilinear };

class Texture {
public:
    static std::shared_ptr<Texture> createWhite();            // 1x1 白图（默认贴图）
    static std::shared_ptr<Texture> createCheckerboard(int cells = 8,
        glm::vec4 colorA = glm::vec4(1.0f),
        glm::vec4 colorB = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
    static std::shared_ptr<Texture> createFromFile(const std::filesystem::path& path, bool srgb = false);
    // 带采样参数的版本（.novameta 消费；默认参数等价旧签名行为）
    static std::shared_ptr<Texture> createFromFile(const std::filesystem::path& path, bool srgb,
                                                   TextureWrap wrap, TextureFilter filter,
                                                   bool generateMipmaps);
    // 已解码 RGBA8 像素直接上传（V0.3-7 二进制缓存命中路径；资产管线内部用，不读盘）
    static std::shared_ptr<Texture> createFromPixels(const uint8_t* rgba8, int width, int height,
                                                     bool srgb, TextureWrap wrap,
                                                     TextureFilter filter, bool generateMipmaps);
    // ===== V1.0-6 追加（cG；新入口，既有签名零变更）：压缩块数据上传 =====
    // blocks = mip0..mipN 连续 4x4 块数据（编码器离线生成完整链，压缩纹理不可
    // glGenerateMipmap）；format 0=BC7 1=BC1 2=BC5（与 BinaryCache 压缩载荷一致）；
    // srgb 仅对 BC7/BC1 生效（sRGB 内部格式变体）；blockBytes 不足所需链字节 → nullptr
    static std::shared_ptr<Texture> createFromCompressed(const uint8_t* blocks, size_t blockBytes,
                                                         int width, int height, int format,
                                                         bool srgb, TextureWrap wrap,
                                                         TextureFilter filter,
                                                         bool generateMipmaps);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void bind(uint32_t slot) const;     // glActiveTexture + glBindTexture
    int width() const;
    int height() const;
    uint32_t handle() const;
    // ===== V1.0 波 1 骨架日（§26.0 冻结项 4）：GPU 占用字节 =====
    // 声明已冻结：实现归 cG（压缩 = 块字节；未压缩 = w*h*4）
    size_t sizeBytes() const;

private:
    Texture() = default;
    uint32_t m_id = 0;
    int m_width = 0, m_height = 0;
    size_t m_sizeBytes = 0;   // V1.0-6：GPU 占用字节（压缩 = 上传块字节；未压缩 = w*h*4）
};

}  // namespace NovaEngine
