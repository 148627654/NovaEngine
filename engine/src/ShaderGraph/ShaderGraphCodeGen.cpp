// ===== V1.0-5 ShaderGraphCodeGen（§27.4）：骨架日桩实现（2026-11-23）=====
// 桩语义 = 未实现：generate 恒失败并给出明确错误文本（面板展示，不静默）。
// 真实实现归 cG（波 2）：节点 → GLSL 生成 + #line 映射表（编译失败时反查节点 id）。
#include <NovaEngine/ShaderGraph/ShaderGraphCodeGen.h>

namespace NovaEngine {

bool ShaderGraphCodeGen::generate(const ShaderGraphAsset& /*graph*/,
                                  std::string& /*outFragmentSource*/,
                                  std::vector<std::string>& outErrors) {
    outErrors.push_back("ShaderGraph 代码生成未实现（cG 波 2 施工中）");
    return false;
}

}  // namespace NovaEngine
