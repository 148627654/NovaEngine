#pragma once
#include <cstdint>
#include <filesystem>

namespace NovaEngine {

// ===== V1.0-4 色彩分级（§27.3 冻结接口，2026-11-23 波 2 骨架日布）=====
// .cube LUT 解析：3D 优先（LUT_3D_SIZE N；DOMAIN_MIN/MAX）；1D 仅解析降级。
// 数据经 AssetStream（pak 兼容）；失败 → loadLut 返回 false + Logger::warn。
// 头文件签名冻结：实现归 cG（engine/src/Render/ColorGrading.cpp）；
// 兜底纪律（永不黑屏契约）：无 LUT 时 C++ 侧必须常驻绑定 1×1 白 3D 纹理到 unit 8。
class ColorGrading {
public:
    static bool loadLut(const std::filesystem::path& relPath);   // 上传 GL_TEXTURE_3D（RGBA16F 优先，降级 RGBA8）
    static void unloadLut();                                     // 清纹理（路径为空/加载失败时 PostProcess 调用）
    static uint32_t lutTexture();                                // 0 = 无 LUT
};

}  // namespace NovaEngine
