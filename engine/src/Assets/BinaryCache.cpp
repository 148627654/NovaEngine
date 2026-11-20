// BinaryCache.cpp —— 资产二进制缓存（V0.3-7）
//
// 目录 assets/.novacache/<fnv1a64 十六进制>.novabin（相对项目根；首次使用懒创建，
// 等效「启动时不存在则创建」；目录总大小 >1GB 打一次 warn）。目录可整体安全删除。
//
// 文件布局（裁定 12；Header 48B，全部小端——x86/x64 原生即小端，
// u32/u64/f32 直接 memcpy 原生值即可，无需字节序转换）：
//   Header: magic "NVBI"(4B) | version u32=1 | payloadType u32 | flags u32
//         | sourceSize u64 | payloadSize u64 | reserved u64 | reserved2 u64
//   payloadType: 1=MeshData, 2=TextureRGBA8, 3=TextureRGBFloat, 4=TextureCompressed（V1.0-6）
//   MeshData 载荷:
//     u32 vertexCount | u32 indexCount
//     | 顶点 48B×N（position f32x3 + normal f32x3 + uv f32x2 + tangent f32x3 + pad f32，
//        字段顺序镜像 Vertex 内存布局，pad 补齐 48B 步长）
//     | 索引 u32×N | AABB 6×f32（min+max） | u32 nameLen | UTF-8 name
//   TextureRGBA8 载荷: u32 w | u32 h | u8 × w*h*4（只存 mip0，GPU 侧 glGenerateMipmap 重建）
//   TextureRGBFloat 载荷: u32 w | u32 h | f32 × w*h*3（HDR equirect）
//   TextureCompressed 载荷（V1.0-6）: u8 format | u32 w | u32 h | u8 mipCount
//     | u64 blockBytes | 块数据 × blockBytes（format 0=BC7 1=BC1 2=BC5；
//     块数据 = mip0..mipN 连续 4x4 块，编码器离线生成，压缩纹理不可 glGenerateMipmap）
//
// 读取校验（裁定 12）：magic / version / payloadType / payloadSize==剩余长度 /
// 载荷内部尺寸；任一不符 → 删除该文件 + warn + false（损坏即删即重导）。
// P2-1：删除必须在读取流关闭之后执行（先读后删），且删除失败打日志可见——
// 流存活期间 remove() 在部分共享模式下静默失败会让坏文件永驻。

#include <NovaEngine/Assets/BinaryCache.h>
#include <NovaEngine/Assets/AssetDatabase.h>   // projectRoot：缓存目录锚点（同模块内依赖）
#include <NovaEngine/Assets/TextureLoader.h>   // V1.0-6：mip 链级数/块字节公式（压缩载荷校验）
#include <NovaEngine/Core/Logger.h>

#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>

namespace NovaEngine {

namespace {

constexpr char kMagic[4] = {'N', 'V', 'B', 'I'};
constexpr uint32_t kVersion = 1;
constexpr uint32_t kTypeMesh = 1;
constexpr uint32_t kTypeTexRgba8 = 2;
constexpr uint32_t kTypeTexRgbFloat = 3;
constexpr uint32_t kTypeTexCompressed = 4;   // V1.0-6（注意：压缩载荷不用 3——3 已被 RGBFloat 占用）
constexpr size_t kHeaderSize = 48;
constexpr size_t kVertexStride = 48;   // 12×f32，pad 1×f32

// 载荷尺寸上限（损坏文件防荒谬分配；正常资产远小于此）
constexpr size_t kMaxVertices = 16u << 20;
constexpr size_t kMaxIndices = 64u << 20;
constexpr size_t kMaxTexDim = 32768;
constexpr size_t kMaxNameLen = 1u << 20;

bool s_dirChecked = false;   // 目录创建 + 1GB 告警只做一次

std::filesystem::path cacheDir() {
    // 项目根优先（编辑器/Player 启动时均 setProjectRoot）；未设置回退当前目录
    std::filesystem::path root = AssetDatabase::projectRoot();
    if (root.empty()) root = std::filesystem::current_path();
    return root / "assets" / ".novacache";
}

bool ensureCacheDir() {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir(), ec);
    if (ec) {
        Logger::warn("[BinaryCache] 缓存目录创建失败: {}", cacheDir().string());
        return false;
    }
    if (s_dirChecked) return true;
    s_dirChecked = true;

    // 启动时（首次使用）目录大小 >1GB 打一次 warn，防缓存无限膨胀
    uint64_t total = 0;
    for (std::filesystem::recursive_directory_iterator it(cacheDir(), ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) total += it->file_size(ec);
    }
    if (total > (1ull << 30))
        Logger::warn("[BinaryCache] 缓存目录超过 1GB（{} MB），建议手动清理 {}",
                     total >> 20, cacheDir().string());
    return true;
}

// ---- 小端序列化辅助（x86 原生，直接 memcpy）----

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    const size_t o = b.size();
    b.resize(o + 4);
    std::memcpy(b.data() + o, &v, 4);
}
void putU64(std::vector<uint8_t>& b, uint64_t v) {
    const size_t o = b.size();
    b.resize(o + 8);
    std::memcpy(b.data() + o, &v, 8);
}
void putBytes(std::vector<uint8_t>& b, const void* p, size_t n) {
    const size_t o = b.size();
    b.resize(o + n);
    if (n) std::memcpy(b.data() + o, p, n);
}

// 有界读取器：所有字段带越界检查（读损坏文件不越界、不崩溃）
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& d) : m_d(d) {}
    bool u8(uint8_t& out) { return take(1, &out); }
    bool u32(uint32_t& out) { return take(4, &out); }
    bool u64(uint64_t& out) { return take(8, &out); }
    bool bytes(void* dst, size_t n) {
        if (m_pos + n > m_d.size()) return false;
        if (n) std::memcpy(dst, m_d.data() + m_pos, n);
        m_pos += n;
        return true;
    }
    size_t remaining() const { return m_d.size() - m_pos; }

private:
    bool take(size_t n, void* out) {
        if (m_pos + n > m_d.size()) return false;
        std::memcpy(out, m_d.data() + m_pos, n);
        m_pos += n;
        return true;
    }
    const std::vector<uint8_t>& m_d;
    size_t m_pos = 0;
};

// 损坏处理：删除该缓存文件 + warn + false（裁定 12「损坏即删即重导」）。
// P2-1：删除失败（文件占用/权限等）必须打日志可见，不得静默吞 error_code——
// 坏文件永驻会让每次导入都走损坏重导路径；调用方保证读取流已关闭（readCacheFile 先读后删）。
bool corrupt(const std::string& key) {
    std::error_code ec;
    const std::filesystem::path file = cacheDir() / (key + ".novabin");
    if (std::filesystem::remove(file, ec)) {
        Logger::warn("[BinaryCache] 缓存损坏，已删除并重导: {}", key);
    } else if (ec) {
        Logger::warn("[BinaryCache] 缓存损坏，删除失败（{}），请手动清理: {}", ec.message(),
                     file.string());
    } else {
        // 文件已不存在（并发删除等）：等价于已删除
        Logger::warn("[BinaryCache] 缓存损坏（文件已不存在），重导: {}", key);
    }
    return false;
}

bool writeCacheFile(const std::string& key, uint32_t payloadType,
                    const std::vector<uint8_t>& payload) {
    if (!ensureCacheDir()) return false;
    std::vector<uint8_t> file;
    file.reserve(kHeaderSize + payload.size());
    // Header 48B（小端）
    file.insert(file.end(), kMagic, kMagic + 4);
    putU32(file, kVersion);
    putU32(file, payloadType);
    putU32(file, 0);           // flags
    putU64(file, 0);           // sourceSize：冻结 API 未携带源字节数，保留为 0
    putU64(file, payload.size());
    putU64(file, 0);           // reserved
    putU64(file, 0);           // reserved2
    file.insert(file.end(), payload.begin(), payload.end());

    std::ofstream out(cacheDir() / (key + ".novabin"), std::ios::binary);
    if (!out || !out.write(reinterpret_cast<const char*>(file.data()),
                           static_cast<std::streamsize>(file.size()))) {
        Logger::warn("[BinaryCache] 写入失败: {}", key);
        return false;
    }
    return true;
}

// 读 + Header 校验（magic/version/payloadType/payloadSize==剩余长度）；未命中静默 false
// P2-1：先读后删——文件流必须在 corrupt() 删除之前关闭。MSVC 流默认共享模式不含
// FILE_SHARE_DELETE 时，流存活期间 remove() 会失败且 error_code 被静默吞掉，
// 损坏文件将永驻（压缩缓存 loadTextureCompressed 同走本函数）。
bool readCacheFile(const std::string& key, uint32_t expectType,
                   std::vector<uint8_t>& outPayload) {
    std::vector<uint8_t> file;
    bool readFailed = false;
    {
        std::ifstream in(cacheDir() / (key + ".novabin"), std::ios::binary);
        if (!in) return false;   // 缓存未命中（正常路径，不打日志）
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size < 0) return false;
        in.seekg(0, std::ios::beg);
        file.resize(static_cast<size_t>(size));
        if (size > 0 &&
            !in.read(reinterpret_cast<char*>(file.data()), size))
            readFailed = true;   // 读取失败 → 损坏（删除延后到流关闭之后）
    }
    if (readFailed) return corrupt(key);   // 流已析构关闭，删除可生效

    if (file.size() < kHeaderSize) return corrupt(key);
    if (std::memcmp(file.data(), kMagic, 4) != 0) return corrupt(key);

    uint32_t version = 0, payloadType = 0;
    uint64_t payloadSize = 0;
    Reader r(file);
    uint32_t magicWord = 0, flags = 0;   // magic 已校验；flags v1 未用
    uint64_t sourceSize = 0, reserved = 0, reserved2 = 0;
    if (!r.u32(magicWord) || !r.u32(version) || !r.u32(payloadType) || !r.u32(flags))
        return corrupt(key);
    // 依次消费完 48B Header：sourceSize / payloadSize / reserved / reserved2
    if (!r.u64(sourceSize) || !r.u64(payloadSize) || !r.u64(reserved) || !r.u64(reserved2))
        return corrupt(key);
    if (version != kVersion) return corrupt(key);
    if (payloadType != expectType) return corrupt(key);
    if (payloadSize != r.remaining()) return corrupt(key);   // payloadSize 与剩余长度一致

    outPayload.assign(file.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), file.end());
    return true;
}

}  // namespace

uint64_t BinaryCache::fnv1a64(const uint8_t* data, size_t size) {
    constexpr uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    uint64_t hash = kOffsetBasis;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= kPrime;
    }
    return hash;
}

std::string BinaryCache::makeKey(const std::vector<uint8_t>& sourceBytes,
                                 const std::string& extraText) {
    uint64_t hash = fnv1a64(sourceBytes.data(), sourceBytes.size());
    if (!extraText.empty()) {
        // 拼接哈希：extraText（贴图 meta 文本）的哈希与源字节哈希再混合一次
        const uint64_t extraHash = fnv1a64(
            reinterpret_cast<const uint8_t*>(extraText.data()), extraText.size());
        uint8_t joined[16];
        std::memcpy(joined, &hash, 8);
        std::memcpy(joined + 8, &extraHash, 8);
        hash = fnv1a64(joined, sizeof(joined));
    }
    return std::format("{:016x}", hash);
}

// ---- MeshData ----

bool BinaryCache::saveMeshData(const std::string& key, const MeshData& mesh) {
    if (key.empty() || mesh.vertices.size() > kMaxVertices ||
        mesh.indices.size() > kMaxIndices || mesh.name.size() > kMaxNameLen) {
        Logger::warn("[BinaryCache] saveMeshData 参数非法: {}", key);
        return false;
    }
    std::vector<uint8_t> payload;
    payload.reserve(8 + mesh.vertices.size() * kVertexStride + mesh.indices.size() * 4 +
                    24 + 4 + mesh.name.size());
    putU32(payload, static_cast<uint32_t>(mesh.vertices.size()));
    putU32(payload, static_cast<uint32_t>(mesh.indices.size()));
    for (const Vertex& v : mesh.vertices) {
        // 48B 步长：position f32x3 + normal f32x3 + uv f32x2 + tangent f32x3 + pad f32
        const float f[12] = {v.position.x, v.position.y, v.position.z,
                             v.normal.x,   v.normal.y,   v.normal.z,
                             v.uv.x,       v.uv.y,
                             v.tangent.x,  v.tangent.y,  v.tangent.z, 0.0f};
        putBytes(payload, f, sizeof(f));
    }
    for (uint32_t idx : mesh.indices) putU32(payload, idx);
    const float aabb[6] = {mesh.aabb.min.x, mesh.aabb.min.y, mesh.aabb.min.z,
                           mesh.aabb.max.x, mesh.aabb.max.y, mesh.aabb.max.z};
    putBytes(payload, aabb, sizeof(aabb));
    putU32(payload, static_cast<uint32_t>(mesh.name.size()));
    putBytes(payload, mesh.name.data(), mesh.name.size());
    return writeCacheFile(key, kTypeMesh, payload);
}

bool BinaryCache::loadMeshData(const std::string& key, MeshData& out) {
    std::vector<uint8_t> payload;
    if (!readCacheFile(key, kTypeMesh, payload)) return false;

    Reader r(payload);
    uint32_t vertexCount = 0, indexCount = 0;
    if (!r.u32(vertexCount) || !r.u32(indexCount)) return corrupt(key);
    if (vertexCount > kMaxVertices || indexCount > kMaxIndices) return corrupt(key);
    // 载荷精确尺寸由逐字段有界读取 + 末尾 remaining==nameLen 校验兜底

    out = MeshData{};
    out.vertices.resize(vertexCount);
    for (Vertex& v : out.vertices) {
        float f[12];
        if (!r.bytes(f, sizeof(f))) return corrupt(key);
        v.position = glm::vec3(f[0], f[1], f[2]);
        v.normal = glm::vec3(f[3], f[4], f[5]);
        v.uv = glm::vec2(f[6], f[7]);
        v.tangent = glm::vec3(f[8], f[9], f[10]);
        // f[11] = pad，忽略
    }
    out.indices.resize(indexCount);
    for (uint32_t& idx : out.indices)
        if (!r.u32(idx)) return corrupt(key);

    float aabb[6];
    if (!r.bytes(aabb, sizeof(aabb))) return corrupt(key);
    out.aabb.min = glm::vec3(aabb[0], aabb[1], aabb[2]);
    out.aabb.max = glm::vec3(aabb[3], aabb[4], aabb[5]);

    uint32_t nameLen = 0;
    if (!r.u32(nameLen) || nameLen > kMaxNameLen) return corrupt(key);
    if (r.remaining() != nameLen) return corrupt(key);
    std::string name(nameLen, '\0');
    if (!r.bytes(name.data(), nameLen)) return corrupt(key);
    out.name = std::move(name);
    return true;
}

// ---- TextureRGBA8（只存 mip0）----

bool BinaryCache::saveTextureRgba8(const std::string& key, int w, int h,
                                   const std::vector<uint8_t>& pixels) {
    if (key.empty() || w <= 0 || h <= 0 || w > static_cast<int>(kMaxTexDim) ||
        h > static_cast<int>(kMaxTexDim) ||
        pixels.size() != static_cast<size_t>(w) * static_cast<size_t>(h) * 4u) {
        Logger::warn("[BinaryCache] saveTextureRgba8 参数非法: {} ({}x{})", key, w, h);
        return false;
    }
    std::vector<uint8_t> payload;
    payload.reserve(8 + pixels.size());
    putU32(payload, static_cast<uint32_t>(w));
    putU32(payload, static_cast<uint32_t>(h));
    putBytes(payload, pixels.data(), pixels.size());
    return writeCacheFile(key, kTypeTexRgba8, payload);
}

bool BinaryCache::loadTextureRgba8(const std::string& key, int& outW, int& outH,
                                   std::vector<uint8_t>& outPixels) {
    std::vector<uint8_t> payload;
    if (!readCacheFile(key, kTypeTexRgba8, payload)) return false;

    Reader r(payload);
    uint32_t w = 0, h = 0;
    if (!r.u32(w) || !r.u32(h)) return corrupt(key);
    if (w == 0 || h == 0 || w > kMaxTexDim || h > kMaxTexDim) return corrupt(key);
    const size_t expect = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    if (r.remaining() != expect) return corrupt(key);

    outPixels.resize(expect);
    if (!r.bytes(outPixels.data(), expect)) return corrupt(key);
    outW = static_cast<int>(w);
    outH = static_cast<int>(h);
    return true;
}

// ---- TextureRGBFloat（HDR equirect）----

bool BinaryCache::saveTextureRgbFloat(const std::string& key, int w, int h,
                                      const std::vector<float>& pixels) {
    if (key.empty() || w <= 0 || h <= 0 || w > static_cast<int>(kMaxTexDim) ||
        h > static_cast<int>(kMaxTexDim) ||
        pixels.size() != static_cast<size_t>(w) * static_cast<size_t>(h) * 3u) {
        Logger::warn("[BinaryCache] saveTextureRgbFloat 参数非法: {} ({}x{})", key, w, h);
        return false;
    }
    std::vector<uint8_t> payload;
    payload.reserve(8 + pixels.size() * sizeof(float));
    putU32(payload, static_cast<uint32_t>(w));
    putU32(payload, static_cast<uint32_t>(h));
    putBytes(payload, pixels.data(), pixels.size() * sizeof(float));
    return writeCacheFile(key, kTypeTexRgbFloat, payload);
}

bool BinaryCache::loadTextureRgbFloat(const std::string& key, int& outW, int& outH,
                                      std::vector<float>& outPixels) {
    std::vector<uint8_t> payload;
    if (!readCacheFile(key, kTypeTexRgbFloat, payload)) return false;

    Reader r(payload);
    uint32_t w = 0, h = 0;
    if (!r.u32(w) || !r.u32(h)) return corrupt(key);
    if (w == 0 || h == 0 || w > kMaxTexDim || h > kMaxTexDim) return corrupt(key);
    const size_t expect = static_cast<size_t>(w) * static_cast<size_t>(h) * 3u;
    if (r.remaining() != expect * sizeof(float)) return corrupt(key);

    outPixels.resize(expect);
    if (!r.bytes(outPixels.data(), expect * sizeof(float))) return corrupt(key);
    outW = static_cast<int>(w);
    outH = static_cast<int>(h);
    return true;
}

// ---- TextureCompressed（V1.0-6；payloadType 4，完整 mip 链块数据）----

bool BinaryCache::saveTextureCompressed(const std::string& key, int format, int w, int h,
                                        const std::vector<uint8_t>& blocks) {
    if (key.empty() || format < 0 || format > 2 || w <= 0 || h <= 0 ||
        w > static_cast<int>(kMaxTexDim) || h > static_cast<int>(kMaxTexDim) || blocks.empty()) {
        Logger::warn("[BinaryCache] saveTextureCompressed 参数非法: {} ({}x{}, fmt {})", key, w,
                     h, format);
        return false;
    }
    // mipCount 由块字节反推：必须精确等于某级数的完整链总字节（1..maxMips），否则拒写
    const int maxMips = TextureLoader::mipLevels(w, h);
    int mipCount = -1;
    for (int m = 1; m <= maxMips; ++m) {
        if (TextureLoader::compressedSizeBytes(format, w, h, m) == blocks.size()) {
            mipCount = m;
            break;
        }
    }
    if (mipCount <= 0) {
        Logger::warn("[BinaryCache] saveTextureCompressed 块字节与尺寸不匹配: {} ({}x{}, {} B)",
                     key, w, h, blocks.size());
        return false;
    }
    std::vector<uint8_t> payload;
    payload.reserve(1 + 4 + 4 + 1 + 8 + blocks.size());
    payload.push_back(static_cast<uint8_t>(format));
    putU32(payload, static_cast<uint32_t>(w));
    putU32(payload, static_cast<uint32_t>(h));
    payload.push_back(static_cast<uint8_t>(mipCount));
    putU64(payload, blocks.size());
    putBytes(payload, blocks.data(), blocks.size());
    return writeCacheFile(key, kTypeTexCompressed, payload);
}

bool BinaryCache::loadTextureCompressed(const std::string& key, int& outFormat, int& outW,
                                        int& outH, std::vector<uint8_t>& outBlocks) {
    std::vector<uint8_t> payload;
    if (!readCacheFile(key, kTypeTexCompressed, payload)) return false;

    Reader r(payload);
    uint8_t format = 0;
    uint32_t w = 0, h = 0;
    uint8_t mipCount = 0;
    uint64_t blockBytes = 0;
    if (!r.u8(format) || !r.u32(w) || !r.u32(h) || !r.u8(mipCount) || !r.u64(blockBytes))
        return corrupt(key);
    if (format > 2 || w == 0 || h == 0 || w > kMaxTexDim || h > kMaxTexDim || mipCount == 0)
        return corrupt(key);
    // mipCount 合法范围 + blockBytes 必须同时等于「剩余长度」与「该级数链总字节」（双校验）
    if (mipCount > TextureLoader::mipLevels(static_cast<int>(w), static_cast<int>(h)))
        return corrupt(key);
    if (blockBytes != r.remaining()) return corrupt(key);
    if (blockBytes != TextureLoader::compressedSizeBytes(format, static_cast<int>(w),
                                                         static_cast<int>(h), mipCount))
        return corrupt(key);

    outBlocks.resize(static_cast<size_t>(blockBytes));
    if (!r.bytes(outBlocks.data(), static_cast<size_t>(blockBytes))) return corrupt(key);
    outFormat = static_cast<int>(format);
    outW = static_cast<int>(w);
    outH = static_cast<int>(h);
    return true;
}

}  // namespace NovaEngine
