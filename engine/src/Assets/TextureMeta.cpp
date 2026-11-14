// TextureMeta.cpp —— .novameta 贴图导入设置读写（V0.3-5）
//
// JSON 伴随文件（nlohmann），与贴图同目录："<贴图名>.<扩展名>.novameta"：
//   {"version":1,"type":"sRGB"|"Linear"|"NormalMap","wrap":"Repeat"|"Clamp",
//    "filter":"Bilinear"|"Trilinear","generateMipmaps":bool}
// 行为约定（冻结头 TextureMeta.h）：
//   缺失 → 写默认 meta 落盘 + metaExisted=false（「无 meta 自动生成」）
//   损坏（解析失败/字段非法）→ 默认值 + warn + metaExisted=false
//   type "NormalMap" → ColorSpace::Linear（法线贴图按线性采样）
// serialize() 返回稳定 JSON 文本（nlohmann 默认对象为 std::map，键按字典序，
// 输出逐字节确定——供二进制缓存 hash 拼接，改 meta 即改 hash，裁定 13）。

#include <NovaEngine/Assets/TextureMeta.h>
#include <NovaEngine/Assets/AssetStream.h>   // P1-1：meta 读取收敛 AssetStream（pak 命中优先、磁盘回退）
#include <NovaEngine/Core/Logger.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>

namespace NovaEngine {

std::filesystem::path TextureMeta::metaPathFor(const std::filesystem::path& texAbsPath) {
    return std::filesystem::path(texAbsPath.string() + ".novameta");
}

std::string TextureMeta::serialize(const TextureImportSettings& s) {
    nlohmann::json j;
    j["version"] = 1;
    j["type"] = (s.colorSpace == TextureImportSettings::ColorSpace::Linear) ? "Linear" : "sRGB";
    j["wrap"] = (s.wrapMode == TextureWrap::Repeat) ? "Repeat" : "Clamp";
    j["filter"] = (s.filterMode == TextureFilter::Trilinear) ? "Trilinear" : "Bilinear";
    j["generateMipmaps"] = s.generateMipmaps;
    // 紧凑文本（无缩进）；std::map 键序 → 稳定输出
    return j.dump();
}

bool TextureMeta::save(const std::filesystem::path& texAbsPath, const TextureImportSettings& s) {
    const std::filesystem::path metaPath = metaPathFor(texAbsPath);
    // 父目录可能不存在（pak 内资产：贴图只存在于内存，磁盘无对应目录）——先建目录
    std::error_code ec;
    std::filesystem::create_directories(metaPath.parent_path(), ec);
    // ifstream/ofstream 的 path 重载：中文路径安全（裁定 1）
    std::ofstream out(metaPath, std::ios::binary);
    if (!out) {
        Logger::warn("[TextureMeta] 写入失败: {}", metaPath.string());
        return false;
    }
    nlohmann::json j;
    j["version"] = 1;
    j["type"] = (s.colorSpace == TextureImportSettings::ColorSpace::Linear) ? "Linear" : "sRGB";
    j["wrap"] = (s.wrapMode == TextureWrap::Repeat) ? "Repeat" : "Clamp";
    j["filter"] = (s.filterMode == TextureFilter::Trilinear) ? "Trilinear" : "Bilinear";
    j["generateMipmaps"] = s.generateMipmaps;
    out << j.dump(4) << '\n';
    return out.good();
}

TextureImportSettings TextureMeta::loadOrCreate(const std::filesystem::path& texAbsPath,
                                                bool& metaExisted) {
    metaExisted = false;
    TextureImportSettings settings;  // 默认值：sRGB / Repeat / Trilinear / 生成 mipmap

    // P1-1：读取收敛 AssetStream（pak 命中优先、磁盘回退）——打包游戏里 .novameta
    // 可能只存在于 pak 内存中，直接 ifstream 读磁盘会静默丢失导入设置。
    const std::filesystem::path metaPath = metaPathFor(texAbsPath);
    std::string content;
    if (!AssetStream::readAllText(metaPath, content)) {
        // 两处都拿不到 → 默认值。仅当「磁盘上确实不存在 meta 且贴图本身是磁盘资产」时
        // 写默认 meta 落盘（pak 内资产不落盘，避免向安装目录写入伴随文件）。
        std::error_code ec;
        const bool metaOnDisk = std::filesystem::exists(metaPath, ec);
        if (!metaOnDisk && std::filesystem::exists(texAbsPath, ec)) {
            // 磁盘资产且无 meta：自动生成默认落盘（验收：无 meta 自动生成）
            if (!save(texAbsPath, settings))
                Logger::warn("[TextureMeta] 默认 meta 生成失败: {}", metaPath.string());
        } else if (metaOnDisk) {
            Logger::warn("[TextureMeta] meta 不可读，回退默认值: {}", metaPath.string());
        }
        return settings;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (const std::exception&) {
        Logger::warn("[TextureMeta] meta 解析失败（损坏），回退默认值: {}", metaPath.string());
        return settings;
    }

    // 字段校验：类型不符或取值非法一律视为损坏（回退默认，不落盘覆盖）
    bool valid = true;
    try {
        if (j.contains("type")) {
            const std::string t = j["type"].get<std::string>();
            if (t == "sRGB") settings.colorSpace = TextureImportSettings::ColorSpace::Srgb;
            else if (t == "Linear") settings.colorSpace = TextureImportSettings::ColorSpace::Linear;
            else if (t == "NormalMap") settings.colorSpace = TextureImportSettings::ColorSpace::Linear;
            else valid = false;
        }
        if (j.contains("wrap")) {
            const std::string t = j["wrap"].get<std::string>();
            if (t == "Repeat") settings.wrapMode = TextureWrap::Repeat;
            else if (t == "Clamp") settings.wrapMode = TextureWrap::ClampToEdge;
            else valid = false;
        }
        if (j.contains("filter")) {
            const std::string t = j["filter"].get<std::string>();
            if (t == "Bilinear") settings.filterMode = TextureFilter::Bilinear;
            else if (t == "Trilinear") settings.filterMode = TextureFilter::Trilinear;
            else valid = false;
        }
        if (j.contains("generateMipmaps")) {
            if (j["generateMipmaps"].is_boolean())
                settings.generateMipmaps = j["generateMipmaps"].get<bool>();
            else valid = false;
        }
    } catch (const std::exception&) {
        valid = false;
    }

    if (!valid) {
        Logger::warn("[TextureMeta] meta 字段非法（损坏），回退默认值: {}", metaPath.string());
        return settings;  // metaExisted 保持 false
    }
    metaExisted = true;
    return settings;
}

}  // namespace NovaEngine
