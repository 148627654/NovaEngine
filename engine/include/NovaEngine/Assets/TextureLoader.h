#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace NovaEngine {

class TextureLoader {
public:
    // 解码为 RGBA8（stb 不翻转：配合 Assimp FlipUVs，避免双翻转）；失败返回 false
    static bool loadRgba8(const std::filesystem::path& absolutePath,
                          int& outWidth, int& outHeight, std::vector<uint8_t>& outPixels);
};

}  // namespace NovaEngine
