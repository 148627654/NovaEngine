// AssetStream.cpp —— 资产字节流统一入口（收官批次枢纽，V0.2-14/V0.5-1）
//
// 磁盘读取走 std::ifstream 的 path 重载（中文路径安全，裁定 1）；
// .novapak 挂载后 exists/readAllBytes/readAllText 先查 pak 内存 entry 再查磁盘
// （pak 未命中回退磁盘：开发期与发布态行为统一；未挂载时与直接读盘逐字节等价）。
// 签名已冻结（mountPak 返回 void），挂载失败经 Logger::error 表达。

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/AssetDatabase.h>   // projectRoot：绝对路径 → pak 相对键降维
#include <NovaEngine/Assets/PakArchive.h>
#include <NovaEngine/Core/Logger.h>

#include <fstream>

namespace NovaEngine {

namespace {

// pak 实例与有效状态：进程内唯一挂载点（Player 启动时 mountPak 一次）
PakArchive s_pak;
bool s_pakValid = false;

// pak entry 键规范形：绝对路径先相对项目根降维（Player：项目根 = exe 目录），
// 再 lexically_normal + 正斜杠（与 PakArchive::pack 入库键一致）。
// 无法降维的绝对路径保持原样（pak 中必然查不到 → 回退磁盘）。
std::string pakLookupPath(const std::filesystem::path& p) {
    std::filesystem::path n = p;
    if (n.is_absolute()) {
        const std::filesystem::path root = AssetDatabase::projectRoot();
        if (!root.empty()) {
            std::error_code ec;
            const std::filesystem::path rel = std::filesystem::relative(n, root, ec);
            if (!ec && !rel.empty() && *rel.begin() != "..") n = rel;
        }
    }
    return n.lexically_normal().generic_string();
}

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
    // PakArchive::open 失败已 Logger::error；此处仅记录挂载结果
    s_pakValid = s_pak.open(pakPath);
    if (s_pakValid) {
        Logger::info("[AssetStream] pak 已挂载: {}", pakPath.string());
    } else {
        Logger::error("[AssetStream] pak 挂载失败，回退磁盘读取: {}", pakPath.string());
    }
}

bool AssetStream::pakMounted() {
    return s_pakValid;   // 返回有效性（open 成功才算挂载）
}

bool AssetStream::exists(const std::filesystem::path& relativePath) {
    if (s_pakValid && s_pak.contains(pakLookupPath(relativePath))) return true;
    std::error_code ec;
    return std::filesystem::exists(relativePath, ec);
}

bool AssetStream::readAllBytes(const std::filesystem::path& relativePath,
                               std::vector<uint8_t>& out) {
    if (s_pakValid && s_pak.read(pakLookupPath(relativePath), out)) return true;
    return readDiskFile(relativePath, out);
}

bool AssetStream::readAllText(const std::filesystem::path& relativePath, std::string& out) {
    std::vector<uint8_t> bytes;
    if (!readAllBytes(relativePath, bytes)) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

}  // namespace NovaEngine
