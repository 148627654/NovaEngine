#pragma once
#include <filesystem>
#include <string>
#include <NovaEngine/Render/Texture.h>   // TextureWrap/TextureFilter 枚举（render 侧定义，assets→render 合法方向）

namespace NovaEngine {

// 贴图导入设置（V0.3-5 .novameta 伴随文件）
struct TextureImportSettings {
    enum class ColorSpace { Srgb, Linear };
    ColorSpace colorSpace = ColorSpace::Srgb;
    TextureWrap wrapMode = TextureWrap::Repeat;
    TextureFilter filterMode = TextureFilter::Trilinear;
    bool generateMipmaps = true;
};

class TextureMeta {
public:
    // 伴随文件路径："<贴图名>.<扩展名>.novameta"（同目录）
    static std::filesystem::path metaPathFor(const std::filesystem::path& texAbsPath);
    // 读取并返回设置；文件缺失/解析失败 → 返回默认值 + metaExisted=false，
    // 且文件缺失时自动生成默认 meta 文件落盘（满足「无 meta 自动生成」验收）
    static TextureImportSettings loadOrCreate(const std::filesystem::path& texAbsPath, bool& metaExisted);
    static bool save(const std::filesystem::path& texAbsPath, const TextureImportSettings& s);
    // 设置 → 稳定 JSON 文本（供二进制缓存 hash 拼接）
    static std::string serialize(const TextureImportSettings& s);
};

}  // namespace NovaEngine
