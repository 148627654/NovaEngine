// TextureLoader.cpp —— stb_image 解码实现
//
// 注意：STB_IMAGE_IMPLEMENTATION 必须且只能在本文件定义一次（全项目唯一实现点），
// 多处定义会导致 LNK2005 重复符号错误。

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/TextureLoader.h>
#include <NovaEngine/Core/Logger.h>

namespace NovaEngine {

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

}  // namespace NovaEngine
