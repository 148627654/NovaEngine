#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace NovaEngine {

// ===== V1.0-5 Shader Graph 图资产（§27.4，2026-11-23 波 2 骨架日布）=====
// 资产格式 .novashadergraph（version 1，存 assets/shadergraphs/）：nodes（id/type/pos/params）
// + edges（from [node,port] → to [node,port]）。节点集 v1 约 16 个（输入/运算/Master，
// 端口带类型，连线类型不匹配 = 拒绝 + 面板提示）。
// 骨架日仅冻结 load/save/validateGraph 签名；节点/边数据模型与校验实现归 cG
//（本文件与 .cpp 为 cG 独占，可自由扩展成员）。
class ShaderGraphAsset {
public:
    static std::shared_ptr<ShaderGraphAsset> load(const std::filesystem::path& relPath);  // AssetStream；失败 nullptr
    static bool save(const std::filesystem::path& relPath, const ShaderGraphAsset& graph);
    // 校验：环/类型不匹配/缺失输入 → 错误列表（明确错误文本，面板直接展示）
    std::vector<std::string> validateGraph() const;
};

}  // namespace NovaEngine
