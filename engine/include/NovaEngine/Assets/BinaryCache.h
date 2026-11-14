#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <NovaEngine/Render/MeshData.h>

namespace NovaEngine {

// 资产二进制缓存（V0.3-7）：assets/.novacache/<fnv1a64 十六进制>.novabin
// magic "NVBI" + version 1 + payloadSize 校验；内容 hash 命名（贴图 hash 拼接 meta 文本）；
// 损坏 → 删除该文件 + 走完整导入重导；目录可整体安全删除
class BinaryCache {
public:
    static uint64_t fnv1a64(const uint8_t* data, size_t size);
    // 缓存键 = fnv1a64(源文件字节) 与 fnv1a64(extraText) 的拼接哈希；extraText 用于贴图 meta 文本
    static std::string makeKey(const std::vector<uint8_t>& sourceBytes, const std::string& extraText = {});

    // MeshData（顶点 48B/个镜像 Vertex 布局 + u32 索引 + AABB 6×f32 + UTF-8 名字）
    static bool loadMeshData(const std::string& key, MeshData& out);
    static bool saveMeshData(const std::string& key, const MeshData& mesh);
    // 贴图 RGBA8（只存 mip0，GPU 侧 glGenerateMipmap 重建）
    static bool loadTextureRgba8(const std::string& key, int& outW, int& outH, std::vector<uint8_t>& outPixels);
    static bool saveTextureRgba8(const std::string& key, int w, int h, const std::vector<uint8_t>& pixels);
    // HDR equirect RGB float
    static bool loadTextureRgbFloat(const std::string& key, int& outW, int& outH, std::vector<float>& outPixels);
    static bool saveTextureRgbFloat(const std::string& key, int w, int h, const std::vector<float>& pixels);
};

}  // namespace NovaEngine
