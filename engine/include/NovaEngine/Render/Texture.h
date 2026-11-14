#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <glm/vec4.hpp>

namespace NovaEngine {

class Texture {
public:
    static std::shared_ptr<Texture> createWhite();            // 1x1 白图（默认贴图）
    static std::shared_ptr<Texture> createCheckerboard(int cells = 8,
        glm::vec4 colorA = glm::vec4(1.0f),
        glm::vec4 colorB = glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
    static std::shared_ptr<Texture> createFromFile(const std::filesystem::path& path, bool srgb = false);
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
