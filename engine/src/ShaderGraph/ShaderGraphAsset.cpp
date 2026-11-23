// ===== V1.0-5 ShaderGraphAsset（§27.4）：骨架日桩实现（2026-11-23）=====
// 桩语义 = 未实现：load nullptr / save false / 校验空列表。真实实现归 cG（波 2）：
// 节点/边数据模型、JSON 往返（version 1）、AssetStream 读写、环与端口类型校验。
#include <NovaEngine/ShaderGraph/ShaderGraphAsset.h>

namespace NovaEngine {

std::shared_ptr<ShaderGraphAsset> ShaderGraphAsset::load(const std::filesystem::path& /*relPath*/) {
    return nullptr;
}
bool ShaderGraphAsset::save(const std::filesystem::path& /*relPath*/,
                            const ShaderGraphAsset& /*graph*/) {
    return false;
}
std::vector<std::string> ShaderGraphAsset::validateGraph() const { return {}; }

}  // namespace NovaEngine
