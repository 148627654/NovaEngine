// AssetStream.cpp —— 资产字节流统一入口（收官批次枢纽，V0.2-14/V0.5-1）
//
// 磁盘读取走 std::ifstream 的 path 重载（中文路径安全，零转换代码，裁定 1）；
// .novapak 挂载层本波（Wave 1）只记录路径并告警，PakArchive 在 Wave 2 落地
// （签名已冻结不能改：mountPak 返回 void，故失败经日志表达）。

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <fstream>

namespace NovaEngine {

namespace {

// pak 挂载状态：Wave 1 只存路径（供 Wave 2 的 PakArchive 延续使用），读取仍全走磁盘
std::filesystem::path s_pakPath;
bool s_pakMounted = false;

// 磁盘读取：ifstream 的 path 重载（MSVC 下内部经宽字符转换，中文路径安全）
bool readDiskFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) return false;
    file.seekg(0, std::ios::beg);

    out.resize(static_cast<size_t>(size));
    if (size > 0 &&
        !file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)))
        return false;
    return true;
}

}  // namespace

void AssetStream::mountPak(const std::filesystem::path& pakPath) {
    // Wave 1：PakArchive 未实现，只记录路径；读取行为不变（仍走磁盘）。
    // Wave 2 实现后：解析 entry 表，exists/readAllBytes/readAllText 按相对路径查内存 entry。
    s_pakPath = pakPath;
    s_pakMounted = true;
    Logger::info("[AssetStream] 已记录 pak 路径: {}", pakPath.string());
    Logger::warn("[AssetStream] pak 支持 Wave 2 落地，本波仍走磁盘读取");
}

bool AssetStream::pakMounted() {
    return s_pakMounted;
}

bool AssetStream::exists(const std::filesystem::path& relativePath) {
    // Wave 1：只查磁盘（pak entry 表 Wave 2 实现）
    std::error_code ec;
    return std::filesystem::exists(relativePath, ec);
}

bool AssetStream::readAllBytes(const std::filesystem::path& relativePath,
                               std::vector<uint8_t>& out) {
    // Wave 1：磁盘读取；pak 挂载后按相对路径查内存 entry 表在 Wave 2 实现
    return readDiskFile(relativePath, out);
}

bool AssetStream::readAllText(const std::filesystem::path& relativePath, std::string& out) {
    std::vector<uint8_t> bytes;
    if (!readAllBytes(relativePath, bytes)) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

}  // namespace NovaEngine
