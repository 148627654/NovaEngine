#include <NovaEngine/Core/FileUtil.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>

#include <fstream>
#include <sstream>

namespace NovaEngine {

std::filesystem::path FileUtil::resolveAbs(const std::filesystem::path& relativePath) {
    return (AssetDatabase::projectRoot() / relativePath).lexically_normal();
}

bool FileUtil::writeText(const std::filesystem::path& relativePath, const std::string& text) {
    const std::filesystem::path abs = resolveAbs(relativePath);
    std::error_code ec;
    if (abs.has_parent_path())
        std::filesystem::create_directories(abs.parent_path(), ec);   // 父目录自动创建
    const std::filesystem::path tmp = abs.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            Logger::error("FileUtil: 写文件失败（无法打开）: {}", abs.string());
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            Logger::error("FileUtil: 写文件失败（写入中断）: {}", abs.string());
            return false;
        }
    }
    // 原子替换：目标机断电/崩溃也不会留下半截文件（与 SaveGame::save 同策略）
    std::filesystem::rename(tmp, abs, ec);
    if (ec) {
        Logger::error("FileUtil: 写文件失败（rename）: {}", abs.string());
        return false;
    }
    return true;
}

bool FileUtil::readText(const std::filesystem::path& relativePath, std::string& out) {
    const std::filesystem::path abs = resolveAbs(relativePath);
    std::ifstream in(abs, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty() || in.good();
}

}  // namespace NovaEngine
