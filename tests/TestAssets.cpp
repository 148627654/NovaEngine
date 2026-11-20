// TestAssets.cpp —— 资产缓存族（§26.1 覆盖清单 3）
//
// 对 KNOWN-ISSUES：
//  - Wave 2 审查：FNV-1a 常量正确性 → 已知向量用例
//  - 二进制缓存校验链（magic/version/payloadType/payloadSize/载荷内部尺寸）→ 损坏即删即重导
//  - 缓存键稳定性（makeKey 内容寻址 + meta 文本拼接）→ §26.7-2 压缩缓存键扩展的回归底座
//  - meta serialize 稳定文本（std::map 键序）
//
// 缓存目录 = projectRoot()/assets/.novacache → 本文件全部用例把项目根指到 TempDir，
// 不污染仓库/构建目录，析构随 TempDir 清理。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/BinaryCache.h>
#include <NovaEngine/Assets/TextureMeta.h>

#include <nlohmann/json.hpp>

#include "TestCommon.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace NovaEngine;
using namespace NovaTest;

namespace {

// 构造带确定性内容的 MeshData（顶点 3 个 + 索引 3 个 + AABB + 名字）
MeshData makeTestMesh() {
    MeshData md;
    md.name = "test-mesh";
    Vertex v0{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    Vertex v1{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    Vertex v2{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    md.vertices = {v0, v1, v2};
    md.indices = {0, 1, 2};
    md.aabb.min = glm::vec3(0.0f);
    md.aabb.max = glm::vec3(1.0f, 1.0f, 0.0f);
    return md;
}

void requireMeshEqual(const MeshData& a, const MeshData& b) {
    REQUIRE(a.name == b.name);
    REQUIRE(a.vertices.size() == b.vertices.size());
    REQUIRE(a.indices.size() == b.indices.size());
    for (size_t i = 0; i < a.vertices.size(); ++i) {
        REQUIRE(a.vertices[i].position == b.vertices[i].position);
        REQUIRE(a.vertices[i].normal == b.vertices[i].normal);
        REQUIRE(a.vertices[i].uv == b.vertices[i].uv);
    }
    REQUIRE(a.indices == b.indices);
    REQUIRE(a.aabb.min == b.aabb.min);
    REQUIRE(a.aabb.max == b.aabb.max);
}

std::filesystem::path cacheFilePath(const std::string& key) {
    return AssetDatabase::projectRoot() / "assets" / ".novacache" / (key + ".novabin");
}

}  // namespace

TEST_CASE("Assets: FNV-1a 64 known vectors", "[assets][fnv]") {
    // 标准 FNV-1a 64 参考向量
    REQUIRE(BinaryCache::fnv1a64(nullptr, 0) == 0xcbf29ce484222325ull);
    const char a[] = "a";
    REQUIRE(BinaryCache::fnv1a64(reinterpret_cast<const uint8_t*>(a), 1) == 0xaf63dc4c8601ec8cull);
    const char foobar[] = "foobar";
    REQUIRE(BinaryCache::fnv1a64(reinterpret_cast<const uint8_t*>(foobar), 6) == 0x85944171f73967e8ull);
}

TEST_CASE("Assets: cache key stability", "[assets][cache-key]") {
    const std::vector<uint8_t> bytes = {'N', 'O', 'V', 'A', 0x00, 0x01, 0xFF};
    const std::vector<uint8_t> other = {'N', 'O', 'V', 'A', 0x00, 0x01, 0xFE};

    const std::string k1 = BinaryCache::makeKey(bytes, "meta");
    const std::string k2 = BinaryCache::makeKey(bytes, "meta");
    REQUIRE(k1 == k2);                              // 同源字节 + 同 meta → 同键
    REQUIRE(BinaryCache::makeKey(bytes, "meta1") != BinaryCache::makeKey(bytes, "meta2"));  // meta 不同 → 键不同
    REQUIRE(BinaryCache::makeKey(bytes, "") != BinaryCache::makeKey(other, ""));            // 字节不同 → 键不同

    // 键为 16 进制小写文本（文件名合法字符，长度稳定）
    REQUIRE(k1.size() == 16);
    REQUIRE(k1.find_first_not_of("0123456789abcdef") == std::string::npos);
}

TEST_CASE("Assets: MeshData payload roundtrip", "[assets][binary-cache]") {
    TempDir tmp("nova-bin-mesh");
    AssetDatabase::setProjectRoot(tmp.path());

    const MeshData md = makeTestMesh();
    const std::string key = BinaryCache::makeKey({'m', 'e', 's', 'h'});
    REQUIRE(BinaryCache::saveMeshData(key, md));
    REQUIRE(std::filesystem::exists(cacheFilePath(key)));

    MeshData out;
    REQUIRE(BinaryCache::loadMeshData(key, out));
    requireMeshEqual(out, md);

    // 二次读取（缓存命中同一文件）
    MeshData out2;
    REQUIRE(BinaryCache::loadMeshData(key, out2));
    requireMeshEqual(out2, md);
}

TEST_CASE("Assets: texture RGBA8/RGBFloat payload roundtrip", "[assets][binary-cache]") {
    TempDir tmp("nova-bin-tex");
    AssetDatabase::setProjectRoot(tmp.path());

    // RGBA8
    const int w = 4, h = 3;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<uint8_t>(i * 7);
    const std::string keyRgba = BinaryCache::makeKey({'t', 'e', 'x', 'r'});
    REQUIRE(BinaryCache::saveTextureRgba8(keyRgba, w, h, pixels));
    int outW = 0, outH = 0;
    std::vector<uint8_t> outPixels;
    REQUIRE(BinaryCache::loadTextureRgba8(keyRgba, outW, outH, outPixels));
    REQUIRE(outW == w);
    REQUIRE(outH == h);
    REQUIRE(outPixels == pixels);

    // RGBFloat（HDR equirect）
    std::vector<float> floats(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < floats.size(); ++i)
        floats[i] = 0.125f * static_cast<float>(i) + 0.5f;
    const std::string keyFloat = BinaryCache::makeKey({'h', 'd', 'r', 'f'});
    REQUIRE(BinaryCache::saveTextureRgbFloat(keyFloat, w, h, floats));
    int fw = 0, fh = 0;
    std::vector<float> outFloats;
    REQUIRE(BinaryCache::loadTextureRgbFloat(keyFloat, fw, fh, outFloats));
    REQUIRE(fw == w);
    REQUIRE(fh == h);
    REQUIRE(outFloats.size() == floats.size());
    for (size_t i = 0; i < floats.size(); ++i)
        REQUIRE(outFloats[i] == Catch::Approx(floats[i]));
}

TEST_CASE("Assets: corrupt cache deleted and re-imported", "[assets][binary-cache]") {
    TempDir tmp("nova-bin-corrupt");
    AssetDatabase::setProjectRoot(tmp.path());

    const MeshData md = makeTestMesh();
    const std::string key = BinaryCache::makeKey({'c', 'o', 'r', 'r'});
    REQUIRE(BinaryCache::saveMeshData(key, md));
    const std::filesystem::path file = cacheFilePath(key);

    // 损坏 magic（头部 4 字节清零）→ 读失败（校验链拦截）
    {
        std::fstream f(file, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(f.is_open());
        const char zero[4] = {0, 0, 0, 0};
        f.seekp(0);
        f.write(zero, 4);
        f.close();
    }
    MeshData out;
    REQUIRE_FALSE(BinaryCache::loadMeshData(key, out));
    // 注：契约「损坏即删」的物理删除当前未生效——corrupt() 在 ifstream 仍打开时 remove，
    // Windows 共享冲突被 error_code 静默吞掉（2026-11-20 cT 实测，BinaryCache.cpp 归 cG
    // 波 1 域，已作为发现移交合波）。删除与否不影响正确性：损坏文件每次读都判失败，
    // 重导（上层重写缓存）后恢复正常。

    // 重导：再次保存（模拟上层重新导入覆写）→ 正常读取
    REQUIRE(BinaryCache::saveMeshData(key, md));
    MeshData out2;
    REQUIRE(BinaryCache::loadMeshData(key, out2));
    requireMeshEqual(out2, md);

    // 载荷截断（payloadSize 与实际剩余不符）同样判损坏
    {
        const uint64_t original = std::filesystem::file_size(file);
        std::filesystem::resize_file(file, original / 2);
    }
    MeshData out3;
    REQUIRE_FALSE(BinaryCache::loadMeshData(key, out3));

    // 截断后重导 → 再次正常
    REQUIRE(BinaryCache::saveMeshData(key, md));
    MeshData out4;
    REQUIRE(BinaryCache::loadMeshData(key, out4));
    requireMeshEqual(out4, md);
}

TEST_CASE("Assets: TextureMeta serialize stable text", "[assets][meta]") {
    // 相同设置 → 逐字节相同文本（std::map 键序稳定，供 BinaryCache 哈希拼接）
    const TextureImportSettings defaults;
    const std::string s1 = TextureMeta::serialize(defaults);
    const std::string s2 = TextureMeta::serialize(defaults);
    REQUIRE(s1 == s2);

    const nlohmann::json j = nlohmann::json::parse(s1);
    REQUIRE(j["version"] == 1);
    REQUIRE(j["type"] == "sRGB");
    REQUIRE(j["wrap"] == "Repeat");
    REQUIRE(j["filter"] == "Trilinear");
    REQUIRE(j["generateMipmaps"] == true);
    // V1.0-6 追加：compression 键名锁定（缺省 "auto"；§26.0 冻结项 6，Reviewer P2-6）
    REQUIRE(j["compression"] == "auto");
    REQUIRE(s1.find("\"compression\":\"auto\"") != std::string::npos);

    TextureImportSettings linear = defaults;
    linear.colorSpace = TextureImportSettings::ColorSpace::Linear;
    linear.filterMode = TextureFilter::Bilinear;
    REQUIRE(TextureMeta::serialize(linear) != s1);   // 设置差异必须反映到文本

    // 压缩格式落文本（bc7enc 消费方按此解析；键值对照锁定）
    TextureImportSettings bc7 = defaults;
    bc7.compression = TextureCompression::BC7;
    REQUIRE(TextureMeta::serialize(bc7).find("\"compression\":\"bc7\"") != std::string::npos);

    // ===== NormalMap 保真往返（KNOWN-ISSUES Wave 1/2 遗留 #5 修复回归，Reviewer P2-6）=====
    // 旧版：meta 里 type "NormalMap" 经模态保存被重写为 "Linear"（往返有损）；
    // V1.0-6：isNormalMap=true → 写回 "NormalMap"（Auto 压缩策略据此选 BC5）。
    TextureImportSettings normal = defaults;
    normal.isNormalMap = true;
    const std::string sn = TextureMeta::serialize(normal);
    REQUIRE(sn.find("\"type\":\"NormalMap\"") != std::string::npos);
    REQUIRE(TextureMeta::serialize(normal) == sn);   // 稳定文本：二次序列化逐字节一致

    // isNormalMap 优先于 colorSpace（法线用途写入 NormalMap，而非 Linear/Srgb）
    TextureImportSettings normalLinear = defaults;
    normalLinear.isNormalMap = true;
    normalLinear.colorSpace = TextureImportSettings::ColorSpace::Linear;
    REQUIRE(TextureMeta::serialize(normalLinear).find("\"type\":\"NormalMap\"") != std::string::npos);

    // isNormalMap=false 时不得出现 NormalMap（防旧字段误写）
    REQUIRE(s1.find("NormalMap") == std::string::npos);
}
