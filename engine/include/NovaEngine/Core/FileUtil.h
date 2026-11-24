#pragma once
#include <filesystem>
#include <string>

namespace NovaEngine {

// 公开文件读写工具（V1.2 竞速包追加；2026-11-24）。
// AssetStream 是只读通道（pak 优先），脚本存档/回放数据需要写通道——本工具
// 只写磁盘（不碰 pak），路径锚点与 AssetDatabase::projectRoot() 同语义
// （编辑器=项目根、Player=exe 目录；「saves/xx」即 SaveGame::saveDirectory() 约定）。
class FileUtil {
public:
    // 相对路径 → 绝对路径（lexically_normal；不含存在性检查）
    static std::filesystem::path resolveAbs(const std::filesystem::path& relativePath);

    // 原子写文本（临时文件 + rename；父目录不存在时自动创建）
    static bool writeText(const std::filesystem::path& relativePath, const std::string& text);

    // 读文本（不存在/读取失败返回 false）
    static bool readText(const std::filesystem::path& relativePath, std::string& out);
};

}  // namespace NovaEngine
