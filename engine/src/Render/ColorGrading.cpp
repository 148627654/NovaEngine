// ===== V1.0-4 ColorGrading（§27.3）：骨架日桩实现（2026-11-23）=====
// 桩语义 = 无 LUT：loadLut 恒 false、纹理恒 0。真实实现归 cG（波 2）：
// .cube 解析 + GL_TEXTURE_3D 上传（RGBA16F 优先降级 RGBA8）+ 1×1 白 3D 兜底纹理
//（unit 8 常驻绑定，永不黑屏契约）。cG 直接在本文件填充实现。
#include <NovaEngine/Render/ColorGrading.h>

namespace NovaEngine {

bool ColorGrading::loadLut(const std::filesystem::path& /*relPath*/) { return false; }
void ColorGrading::unloadLut() {}
uint32_t ColorGrading::lutTexture() { return 0; }

}  // namespace NovaEngine
