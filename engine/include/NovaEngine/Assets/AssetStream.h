#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace NovaEngine {

// 资产字节流统一入口（收官批次枢纽）：磁盘读取（ifstream path 重载，中文路径安全）
// + .novapak 挂载层（Player 运行期）。pak 未挂载时行为与直接读磁盘逐字节等价。
class AssetStream {
public:
    static void mountPak(const std::filesystem::path& pakPath);   // Player 启动时调用；失败 Logger::error 返回
    static bool pakMounted();
    static bool exists(const std::filesystem::path& relativePath);          // 先查 pak 再查磁盘
    static bool readAllBytes(const std::filesystem::path& relativePath, std::vector<uint8_t>& out);
    static bool readAllText(const std::filesystem::path& relativePath, std::string& out);
};

}  // namespace NovaEngine
