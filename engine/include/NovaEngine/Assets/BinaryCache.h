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
    // ===== V1.0-6 追加：贴图压缩载荷（payloadType 4；既有 payloadType 1/2/3 不动、版本不 bump）=====
    // 载荷布局：[format:u8][w:u32][h:u32][mipCount:u8][blockBytes:u64][块数据]
    //   format 0=BC7 1=BC1 2=BC5；块数据 = mip0..mipN 连续 4x4 块（编码器离线生成完整链，
    //   压缩纹理不可 glGenerateMipmap）；blockBytes 必须精确等于该 mipCount 的链总字节
    // 缓存键（调用方拼接）：makeKey(源字节, meta 文本 + ";comp=<format>;caps=<能力标志>")
    static bool loadTextureCompressed(const std::string& key, int& outFormat, int& outW, int& outH,
                                      std::vector<uint8_t>& outBlocks);
    static bool saveTextureCompressed(const std::string& key, int format, int w, int h,
                                      const std::vector<uint8_t>& blocks);
    // HDR equirect RGB float
    static bool loadTextureRgbFloat(const std::string& key, int& outW, int& outH, std::vector<float>& outPixels);
    static bool saveTextureRgbFloat(const std::string& key, int w, int h, const std::vector<float>& pixels);
};

}  // namespace NovaEngine
