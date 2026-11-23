// ===== V1.0-4 ColorGrading（§27.3）真实实现（cG，2026-11-23）=====
//
// .cube LUT 解析与 GL_TEXTURE_3D 上传：
//   - 3D 优先（LUT_3D_SIZE N；DOMAIN_MIN/MAX 缩放）；1D（LUT_1D_SIZE M）仅解析降级——
//     1D 语义 = 逐通道曲线，展开为 17³ 3D 网格（每通道独立映射，精确降级，采样段零改动）。
//   - 数据经 AssetStream（pak 兼容）；失败 → loadLut 返回 false + Logger::warn。
//   - 上传格式 RGBA16F 优先（GL_RGBA16F + GL_FLOAT 数据），glGetError 探测失败降级 RGBA8。
//   - 数据顺序约定（Adobe .cube 规范）：文件内 red 最慢、blue 最快（顺序行 = b 先步进）；
//     GL 纹理内存 x 最快——CPU 重排为 x=r, y=g, z=b，使 post_combine.frag 直接
//     texture(uLut, color.rgb) 采样（冻结段不改）。
//   - 兜底纪律（永不黑屏契约）：无 LUT 时 PostProcess 侧常驻 1×1 白 3D 纹理绑 unit 8
//     （本文件只负责 LUT 本体；白兜底归 PostProcess.cpp，见其注释）。

#include <NovaEngine/Render/ColorGrading.h>

#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

namespace NovaEngine {

namespace {

// ===== 内部状态 =====
uint32_t s_lutTex = 0;    // 当前 LUT 3D 纹理 id（0 = 无 LUT）
bool s_is16f = false;     // 上传内部格式（统计/诊断日志用）
int s_lutSize = 0;        // 3D 边长（诊断日志用）

// RGBA16F 优先上传；GL 错误 → RGBA8 降级。rgb 布局 = x 最快（x=r,y=g,z=b），长度 n³*3。
bool uploadLut3d(int n, const std::vector<float>& rgb) {
    // 组装 RGBA（alpha=1）
    std::vector<float> rgba(static_cast<size_t>(n) * n * n * 4, 1.0f);
    for (size_t i = 0; i < rgb.size() / 3; ++i) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
    }

    // 上传过程不扰动调用方的纹理绑定（PostProcess 帧内调用）
    GLint prevActive = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    GLint prev3d = 0;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &prev3d);

    glGenTextures(1, &s_lutTex);
    glBindTexture(GL_TEXTURE_3D, s_lutTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // 先试 RGBA16F（线性采样无损；§27.3 优先格式）
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, n, n, n, 0, GL_RGBA, GL_FLOAT, rgba.data());
    GLenum err = glGetError();
    s_is16f = (err == GL_NO_ERROR);
    if (!s_is16f) {
        // 降级 RGBA8（防御级：桌面 GL 4.5 上 16F 3D 应恒可用）
        std::vector<uint8_t> bytes(rgba.size());
        for (size_t i = 0; i < rgba.size(); ++i)
            bytes[i] = static_cast<uint8_t>(std::clamp(rgba[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, n, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     bytes.data());
        Logger::warn("[ColorGrading] RGBA16F 3D 上传失败，已降级 RGBA8（LUT 精度降低）");
    }

    // 三线性 + 三轴 CLAMP（LUT 越界钳制，防边缘渗色）
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, static_cast<uint32_t>(prev3d));
    glActiveTexture(static_cast<GLenum>(prevActive));
    return true;
}

}  // namespace

bool ColorGrading::loadLut(const std::filesystem::path& relPath) {
    unloadLut();   // 任何失败路径都落到「无 LUT」状态（永不黑屏契约）

    std::string text;
    if (relPath.empty() || !AssetStream::readAllText(relPath, text)) {
        Logger::warn("[ColorGrading] 无法读取 .cube 文件: {}", relPath.string());
        return false;
    }

    // ===== 解析：头关键字 + 数据行（3 float/行）=====
    int lut3d = 0, lut1d = 0;
    float domainMin[3] = {0.0f, 0.0f, 0.0f};
    float domainMax[3] = {1.0f, 1.0f, 1.0f};
    bool hasDomain = false;
    std::vector<float> data;   // 每行 r g b
    std::istringstream file(text);
    std::string line;
    while (std::getline(file, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);   // 去行尾注释
        std::istringstream ls(line);
        std::string tok;
        if (!(ls >> tok)) continue;   // 空行/纯注释
        if (tok == "TITLE") continue;
        if (tok == "LUT_3D_SIZE") { ls >> lut3d; continue; }
        if (tok == "LUT_1D_SIZE") { ls >> lut1d; continue; }
        if (tok == "DOMAIN_MIN") {
            hasDomain = true;
            ls >> domainMin[0] >> domainMin[1] >> domainMin[2];
            continue;
        }
        if (tok == "DOMAIN_MAX") {
            hasDomain = true;
            ls >> domainMax[0] >> domainMax[1] >> domainMax[2];
            continue;
        }
        // 数据行：首 token 已被取出，与剩余两个值合成三元组
        float r = 0.0f, g = 0.0f, b = 0.0f;
        try {
            r = std::stof(tok);
        } catch (...) {
            Logger::warn("[ColorGrading] .cube 数据行无法解析（已跳过）: {}", line);
            continue;
        }
        if (!(ls >> g >> b)) {
            Logger::warn("[ColorGrading] .cube 数据行分量不足（已跳过）: {}", line);
            continue;
        }
        data.insert(data.end(), {r, g, b});
    }

    // DOMAIN 缩放辅助：存在 DOMAIN 且非 [0,1] 时把条目归一化进 [0,1]（v1 尽力语义）
    const auto scaleToDomain = [&](float v, int axis) {
        const float d = domainMax[axis] - domainMin[axis];
        if (hasDomain && d > 1e-6f)
            return std::clamp((v - domainMin[axis]) / d, 0.0f, 1.0f);
        return std::clamp(v, 0.0f, 1.0f);
    };

    // ===== 3D 路径（优先）=====
    if (lut3d >= 2 && lut3d <= 64) {
        const int n = lut3d;
        const size_t need = static_cast<size_t>(n) * n * n;
        if (data.size() / 3 < need) {
            Logger::warn("[ColorGrading] .cube 3D 数据行不足（需 {}，实得 {}）: {}", need,
                         data.size() / 3, relPath.string());
            return false;
        }
        // 重排：文件序 red 最慢/blue 最快 → GL 内存 x 最快（x=r, y=g, z=b）
        std::vector<float> reordered(need * 3);
        for (int r = 0; r < n; ++r)
            for (int g = 0; g < n; ++g)
                for (int b = 0; b < n; ++b) {
                    const size_t in = (static_cast<size_t>(r) * n + g) * n + b;
                    const size_t out = (static_cast<size_t>(b) * n + g) * n + r;   // 等价 r + g*n + b*n²
                    reordered[out * 3 + 0] = scaleToDomain(data[in * 3 + 0], 0);
                    reordered[out * 3 + 1] = scaleToDomain(data[in * 3 + 1], 1);
                    reordered[out * 3 + 2] = scaleToDomain(data[in * 3 + 2], 2);
                }
        s_lutSize = n;
        uploadLut3d(n, reordered);
        Logger::info("[ColorGrading] LUT 已加载: {}（3D {}³，{}）", relPath.string(), n,
                     s_is16f ? "RGBA16F" : "RGBA8");
        return true;
    }

    // ===== 1D 路径（仅解析降级）=====
    if (lut1d >= 2 && lut1d <= 65536) {
        const int m = lut1d;
        if (data.size() / 3 < static_cast<size_t>(m)) {
            Logger::warn("[ColorGrading] .cube 1D 数据行不足（需 {}，实得 {}）: {}", m,
                         data.size() / 3, relPath.string());
            return false;
        }
        // 逐通道曲线（线性插值采样）
        const auto curve = [&](float coord, int axis) {
            const float f = std::clamp(coord, 0.0f, 1.0f) * static_cast<float>(m - 1);
            const int i0 = std::min(static_cast<int>(f), m - 2);
            const float t = f - static_cast<float>(i0);
            const float v0 = scaleToDomain(data[static_cast<size_t>(i0) * 3 + axis], axis);
            const float v1 = scaleToDomain(data[static_cast<size_t>(i0 + 1) * 3 + axis], axis);
            return v0 + (v1 - v0) * t;
        };
        // 展开为 17³ 3D 网格：out(x,y,z) = (curve(x).r, curve(y).g, curve(z).b)
        //（1D LUT 语义即逐通道映射，此为精确展开而非近似）
        constexpr int kGrid = 17;
        std::vector<float> grid(static_cast<size_t>(kGrid) * kGrid * kGrid * 3);
        for (int x = 0; x < kGrid; ++x)
            for (int y = 0; y < kGrid; ++y)
                for (int z = 0; z < kGrid; ++z) {
                    const float cx = static_cast<float>(x) / (kGrid - 1);
                    const float cy = static_cast<float>(y) / (kGrid - 1);
                    const float cz = static_cast<float>(z) / (kGrid - 1);
                    const size_t idx = (static_cast<size_t>(z) * kGrid + y) * kGrid + x;
                    grid[idx * 3 + 0] = curve(cx, 0);
                    grid[idx * 3 + 1] = curve(cy, 1);
                    grid[idx * 3 + 2] = curve(cz, 2);
                }
        s_lutSize = kGrid;
        uploadLut3d(kGrid, grid);
        Logger::info("[ColorGrading] LUT 已加载（1D {} 降级为 {}³ 逐通道网格）: {}", m, kGrid,
                     relPath.string());
        return true;
    }

    Logger::warn("[ColorGrading] .cube 缺少合法 LUT_3D_SIZE/LUT_1D_SIZE（或无数据）: {}",
                 relPath.string());
    return false;
}

void ColorGrading::unloadLut() {
    if (s_lutTex) {
        glDeleteTextures(1, &s_lutTex);
        s_lutTex = 0;
    }
    s_lutSize = 0;
    s_is16f = false;
}

uint32_t ColorGrading::lutTexture() {
    return s_lutTex;
}

}  // namespace NovaEngine
