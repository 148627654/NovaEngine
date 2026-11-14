#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace NovaEngine {

// .novapak 单文件资产包（V0.5-1）
// 布局（小端）：Header 16B（magic "NVPK" | version 1 | entryCount | indexOffset=16）
// + entry 表（u32 pathLen + UTF-8 正斜杠相对路径 + u64 offset + u64 size + u32 flags）
// + payload（原始字节，无压缩）
// v1：挂载时全量读入内存（面向 <100MB 包体；流式读取列后续）
class PakArchive {
public:
    // files: (项目内相对路径, 源文件绝对路径)；写入失败 Logger::error 返回 false
    static bool pack(const std::filesystem::path& pakPath,
                     const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& files);

    bool open(const std::filesystem::path& pakPath);   // 失败 Logger::error 返回 false
    bool isValid() const;
    bool contains(const std::filesystem::path& relativePath) const;   // 相对路径须为正斜杠
    bool read(const std::filesystem::path& relativePath, std::vector<uint8_t>& out) const;  // 未找到返回 false

private:
    struct Entry { std::string path; uint64_t offset = 0; uint64_t size = 0; };
    std::vector<Entry> m_entries;
    std::vector<uint8_t> m_blob;   // 整个 pak 文件内容（v1 全量驻留内存）
};

}  // namespace NovaEngine
