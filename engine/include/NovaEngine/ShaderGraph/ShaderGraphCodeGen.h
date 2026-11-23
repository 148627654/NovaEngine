#pragma once
#include <string>
#include <vector>

namespace NovaEngine {

class ShaderGraphAsset;

// ===== V1.0-5 Shader Graph 代码生成（§27.4，2026-11-23 波 2 骨架日布）=====
// 生成 GLSL 片元源码（含 #line + 节点注释映射——编译错误可定位到节点）；
// 顶点部分复用 blinn_phong.vert（不生成 vs）；uniform 契约与 blinn_phong.frag 完全一致
//（CameraBlock/uLightDir/uLightColor/uAmbient/uView/材质槽 uniform/sampler2D unit 0-5——
//  PBR 光照函数模板内嵌生成器，与 blinn_phong.frag 同步维护、源码注释互指——
//  错位 = 花屏，评审逐名核对）。签名冻结；实现归 cG。
class ShaderGraphCodeGen {
public:
    // 生成成功返回 true；失败时 outErrors 为节点级错误文本（行号经 #line 映射回节点 id）
    static bool generate(const ShaderGraphAsset& graph, std::string& outFragmentSource,
                         std::vector<std::string>& outErrors);
};

}  // namespace NovaEngine
