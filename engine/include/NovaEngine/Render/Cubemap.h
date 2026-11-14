#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace NovaEngine {

// 立方体贴图（V0.2-7）：6 面图或等距柱状图（HDR/LDR）转换；内部线性存储（GL_RGB16F）
class Cubemap {
public:
    // faces 顺序：+X, -X, +Y, -Y, +Z, -Z（GL cubemap face 约定）；失败返回 nullptr + Logger::error
    static std::shared_ptr<Cubemap> createFromFaces(const std::array<std::filesystem::path, 6>& facePaths,
                                                    bool srgb = true);
    // 等距柱状 2D 图（.hdr/.png/.jpg/.bmp）：临时 FBO 渲染 6 面转换（faceSize²/面）；失败返回 nullptr
    static std::shared_ptr<Cubemap> createFromEquirect(const std::filesystem::path& absolutePath,
                                                       int faceSize = 1024);
    ~Cubemap();                        // glDeleteTextures（GPU 资源释放）
    Cubemap(const Cubemap&) = delete;
    Cubemap& operator=(const Cubemap&) = delete;
    void bind(uint32_t unit) const;    // glActiveTexture + bind cube map
    uint32_t handle() const;
    int faceSize() const;

private:
    Cubemap() = default;
    uint32_t m_id = 0;
    int m_faceSize = 0;
};

}  // namespace NovaEngine
