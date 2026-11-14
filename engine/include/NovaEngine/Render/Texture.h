#pragma once
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
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void bind(uint32_t slot) const;     // glActiveTexture + glBindTexture
    int width() const;
    int height() const;
    uint32_t handle() const;

private:
    Texture() = default;
    uint32_t m_id = 0;
    int m_width = 0, m_height = 0;
};

}  // namespace NovaEngine
