#pragma once
#include <string>
#include <utility>
#include <vector>

namespace NovaEngine {

class ShaderGraphAsset;

// ===== V1.0-5 Shader Graph 代码生成（§27.4，2026-11-23 波 2 骨架日布）=====
// 生成 GLSL 片元源码（含 #line + 节点注释映射——编译错误可定位到节点）；
// 顶点部分复用 blinn_phong.vert（不生成 vs）；uniform 契约与 blinn_phong.frag 完全一致
//（CameraBlock/uLightDir/uLightColor/uAmbient/uView/材质槽 uniform/sampler2D unit 0-5/
//  阴影/IBL/uGamma——PBR 光照函数模板内嵌生成器，与 blinn_phong.frag 同步维护、
//  源码注释互指——错位 = 花屏，评审逐名核对）。
// 骨架日冻结签名：generate；以下为 cG 扩展（本文件 cG 独占，可扩展）。
class ShaderGraphCodeGen {
public:
    // 生成成功返回 true；失败时 outErrors 为节点级错误文本（含节点 id，面板据此高亮）。
    // 成功时同步构建 #line 映射表与纹理路径表（见 lastLineMap/lastTexturePaths）。
    static bool generate(const ShaderGraphAsset& graph, std::string& outFragmentSource,
                         std::vector<std::string>& outErrors);

    // ===== 以下为 cG 扩展 =====
    // 最近一次 generate 的 #line 映射表：（源码行号 → 节点 id），编译失败仍可用
    //（表在代码生成期构建，早于任何编译；GL log 行号经此反查节点）。
    static const std::vector<std::pair<int, std::string>>& lastLineMap();
    // 最近一次 generate 的图纹理节点路径（顺序 = 生成 sampler gTex0.. 的顺序；
    // C++ 提交路径按 unit 9+i 绑定同序路径，§27.4 采样器契约）。
    static std::vector<std::string> lastTexturePaths();
    // 编译生成片元（vs 自动读 assets/shaders/blinn_phong.vert）并返回完整 GL 编译 log；
    // 成功返回 true（log 为空）。与 Shader::createFromSource 的差别 = log 不外泄到 Logger，
    // 供面板错误定位与高亮。
    static bool compileGenerated(const std::string& fragmentSource, std::string& outLog);

    // 节点级错误（面板高亮用）
    struct GraphNodeError {
        int line = 0;              // GL log 原始行号（0 = 定位失败，兜底看完整 log）
        std::string nodeId;        // 反查出的节点 id（空 = 定位失败）
        std::string message;       // 该行错误文本
    };
    // GL 编译 log → 节点级错误列表（行号经 lastLineMap 反查；含 ±1 容差防驱动行号偏移）
    static std::vector<GraphNodeError> mapCompileErrors(const std::string& log);
};

}  // namespace NovaEngine
