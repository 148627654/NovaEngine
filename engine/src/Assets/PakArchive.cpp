// PakArchive.cpp —— .novapak 单文件资产包（V0.5-1）
//
// 布局（小端；x86 原生直接 memcpy）：
//   Header 16B：magic "NVPK"(4B) | version u32=1 | entryCount u32 | indexOffset u32=16
//   （4+4+4+4=16B；entry 表起点，v1 恒为 16 紧跟 Header）
//   + entry 表：u32 pathLen | UTF-8 正斜杠相对路径 | u64 offset | u64 size | u32 flags
//   + payload：原始字节，无压缩（裁定 11：zlib 不复用——assimp 内嵌实现不导出）
// v1：open 全量读入内存（面向 <100MB 包体；流式读取列后续，裁定 §11）。
// 路径统一正斜杠（generic_string + lexically_normal）。

#include <NovaEngine/Assets/PakArchive.h>
#include <NovaEngine/Core/Logger.h>

#include <cstring>
#include <fstream>

namespace NovaEngine {

namespace {

constexpr char kMagic[4] = {'N', 'V', 'P', 'K'};
constexpr uint32_t kVersion = 1;
constexpr uint64_t kHeaderSize = 16;

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

bool readU32At(const std::vector<uint8_t>& d, size_t pos, uint32_t& out) {
    if (pos + 4 > d.size()) return false;
    std::memcpy(&out, d.data() + pos, 4);
    return true;
}
bool readU64At(const std::vector<uint8_t>& d, size_t pos, uint64_t& out) {
    if (pos + 8 > d.size()) return false;
    std::memcpy(&out, d.data() + pos, 8);
    return true;
}

// entry 键规范形：正斜杠 + 词法归一（"./a/../b" → "b"）
std::string normPath(const std::filesystem::path& p) {
    return p.lexically_normal().generic_string();
}

}  // namespace

bool PakArchive::pack(const std::filesystem::path& pakPath,
                      const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& files) {
    if (files.empty()) {
        Logger::error("[PakArchive] 打包文件列表为空: {}", pakPath.string());
        return false;
    }

    struct Item {
        std::string path;
        std::vector<uint8_t> bytes;
    };
    std::vector<Item> items;
    items.reserve(files.size());

    size_t tableSize = 0;
    size_t totalPayload = 0;
    for (const auto& [rel, abs] : files) {
        // 缺失资源打包即报错（裁定 11）
        std::ifstream in(abs, std::ios::binary);
        if (!in) {
            Logger::error("[PakArchive] 打包源文件缺失: {}", abs.string());
            return false;
        }
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size < 0) {
            Logger::error("[PakArchive] 读取失败: {}", abs.string());
            return false;
        }
        in.seekg(0, std::ios::beg);

        Item item;
        item.path = normPath(rel);
        item.bytes.resize(static_cast<size_t>(size));
        if (size > 0 &&
            !in.read(reinterpret_cast<char*>(item.bytes.data()), static_cast<std::streamsize>(size))) {
            Logger::error("[PakArchive] 读取失败: {}", abs.string());
            return false;
        }
        tableSize += 4 + item.path.size() + 8 + 8 + 4;
        totalPayload += item.bytes.size();
        items.push_back(std::move(item));
    }

    std::vector<uint8_t> blob;
    blob.reserve(kHeaderSize + tableSize + totalPayload);

    // Header 16B（4+4+4+4；indexOffset u32）
    blob.insert(blob.end(), kMagic, kMagic + 4);
    putU32(blob, kVersion);
    putU32(blob, static_cast<uint32_t>(items.size()));
    putU32(blob, static_cast<uint32_t>(kHeaderSize));   // indexOffset = 16

    // entry 表（payload offset = 16 + 表长 + 已写 payload 累计）
    uint64_t offset = kHeaderSize + tableSize;
    for (const Item& item : items) {
        putU32(blob, static_cast<uint32_t>(item.path.size()));
        blob.insert(blob.end(), item.path.begin(), item.path.end());
        putU64(blob, offset);
        putU64(blob, item.bytes.size());
        putU32(blob, 0);   // flags（v1 未用）
        offset += item.bytes.size();
    }

    // payload（原始字节，无压缩）
    for (const Item& item : items)
        blob.insert(blob.end(), item.bytes.begin(), item.bytes.end());

    std::ofstream out(pakPath, std::ios::binary);
    if (!out || !out.write(reinterpret_cast<const char*>(blob.data()),
                           static_cast<std::streamsize>(blob.size()))) {
        Logger::error("[PakArchive] 写入 pak 失败: {}", pakPath.string());
        return false;
    }
    Logger::info("[PakArchive] 打包完成: {}（{} 个文件，{} KB）", pakPath.string(),
                 items.size(), blob.size() / 1024);
    return true;
}

bool PakArchive::open(const std::filesystem::path& pakPath) {
    m_entries.clear();
    m_blob.clear();

    std::ifstream in(pakPath, std::ios::binary);
    if (!in) {
        Logger::error("[PakArchive] 打开失败: {}", pakPath.string());
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        Logger::error("[PakArchive] 读取失败: {}", pakPath.string());
        return false;
    }
    in.seekg(0, std::ios::beg);
    m_blob.resize(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(m_blob.data()), size)) {
        Logger::error("[PakArchive] 读取失败: {}", pakPath.string());
        m_blob.clear();
        return false;
    }

    // 校验 magic / version / indexOffset 越界防护（损坏 → error + false）
    auto fail = [&](const char* reason) {
        Logger::error("[PakArchive] 损坏（{}）: {}", reason, pakPath.string());
        m_entries.clear();
        m_blob.clear();
        return false;
    };
    if (m_blob.size() < kHeaderSize) return fail("文件过小");
    if (std::memcmp(m_blob.data(), kMagic, 4) != 0) return fail("magic 不符");

    uint32_t version = 0, entryCount = 0, indexOffsetU32 = 0;
    if (!readU32At(m_blob, 4, version) || !readU32At(m_blob, 8, entryCount) ||
        !readU32At(m_blob, 12, indexOffsetU32))
        return fail("header 截断");
    const uint64_t indexOffset = indexOffsetU32;   // Header 16B：indexOffset 为 u32
    if (version != kVersion) return fail("版本不符");
    if (indexOffset < kHeaderSize || indexOffset > m_blob.size()) return fail("indexOffset 越界");

    // entry 表自 indexOffset 起（v1 恒为 16，紧跟 Header）；全部字段按 blob 尺寸越界防护
    size_t pos = static_cast<size_t>(indexOffset);
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint32_t pathLen = 0;
        if (!readU32At(m_blob, pos, pathLen)) return fail("entry 表截断");
        pos += 4;
        if (pathLen > m_blob.size() - pos) return fail("entry 路径越界");

        Entry e;
        e.path.assign(reinterpret_cast<const char*>(m_blob.data() + pos), pathLen);
        pos += pathLen;

        uint64_t offset = 0, sizeOf = 0;
        uint32_t flags = 0;
        if (!readU64At(m_blob, pos, offset) || !readU64At(m_blob, pos + 8, sizeOf) ||
            !readU32At(m_blob, pos + 16, flags))
            return fail("entry 字段截断");
        pos += 20;
        // payload offset/size 必须落在 [indexOffset, blobSize)：防越界读
        if (offset < indexOffset || offset > m_blob.size() ||
            sizeOf > m_blob.size() - offset)
            return fail("entry offset/size 越界");

        e.offset = offset;
        e.size = sizeOf;
        m_entries.push_back(std::move(e));
    }

    Logger::info("[PakArchive] pak 已打开: {}（{} 个条目，{} KB）", pakPath.string(),
                 m_entries.size(), m_blob.size() / 1024);
    return true;
}

bool PakArchive::isValid() const {
    return !m_blob.empty();
}

bool PakArchive::contains(const std::filesystem::path& relativePath) const {
    // 线性查找：v1 包体 entry 少（<10^3 量级），O(n) 足够，无需哈希表
    const std::string path = normPath(relativePath);
    for (const Entry& e : m_entries)
        if (e.path == path) return true;
    return false;
}

bool PakArchive::read(const std::filesystem::path& relativePath, std::vector<uint8_t>& out) const {
    const std::string path = normPath(relativePath);
    for (const Entry& e : m_entries) {
        if (e.path != path) continue;
        const size_t begin = static_cast<size_t>(e.offset);
        out.assign(m_blob.begin() + static_cast<std::ptrdiff_t>(begin),
                   m_blob.begin() + static_cast<std::ptrdiff_t>(begin + e.size));
        return true;
    }
    return false;   // 未找到
}

}  // namespace NovaEngine
