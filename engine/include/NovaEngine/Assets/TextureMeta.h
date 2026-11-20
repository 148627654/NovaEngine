#pragma once
#include <filesystem>
#include <string>
#include <NovaEngine/Render/Texture.h>   // TextureWrap/TextureFilter 枚举（render 侧定义，assets→render 合法方向）

namespace NovaEngine {

// 贴图压缩格式（V1.0-6）：Auto = 自动策略（法线 BC5 / 含有效 alpha BC7 / 其余 BC1）
enum class TextureCompression { Auto, None, BC1, BC7, BC5 };

// 贴图导入设置（V0.3-5 .novameta 伴随文件；V1.0-6 追加 compression/isNormalMap，既有字段不变）
struct TextureImportSettings {
    enum class ColorSpace { Srgb, Linear };
    ColorSpace colorSpace = ColorSpace::Srgb;
    TextureWrap wrapMode = TextureWrap::Repeat;
    TextureFilter filterMode = TextureFilter::Trilinear;
    bool generateMipmaps = true;
    // ===== V1.0-6 追加（旧 meta 无该字段读取缺省 Auto/零警告）=====
    TextureCompression compression = TextureCompression::Auto;
    // 用途为法线（.novameta type "NormalMap"；Auto 策略据此选 BC5）。
    // 旧编辑器「NormalMap 读出后重写为 Linear」往返有损（KNOWN-ISSUES #5），V1.0-6 起保真。
    bool isNormalMap = false;
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
