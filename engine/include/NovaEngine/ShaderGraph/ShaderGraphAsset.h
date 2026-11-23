#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace NovaEngine {

// ===== V1.0-5 Shader Graph 图资产（§27.4；本文件 cG 独占，数据模型可扩展）=====
// 资产格式 .novashadergraph（version 1，存 assets/shadergraphs/）：
//   { "version": 1,
//     "nodes": [ { "id": "n1", "type": "TextureSample", "pos": [80, 120],
//                  "params": { "path": "assets/textures/checker.png" } },
//                { "id": "master", "type": "Master", "pos": [640, 200], "params": {} } ],
//     "edges": [ { "from": ["n1", "rgba"], "to": ["master", "albedo"] } ] }
// params 值统一字符串（float/颜色/路径皆字符串化）→ JSON 往返无损（§27.4 验收 3）。

// 端口类型（§27.4：float/vec2/vec3/vec4/sampler2D）。
// 隐式转换规则（isConvertible）：数值类型间互通（GLSL 构造语义：升维 splat / 降维截断），
// sampler2D 与任何其它类型不互通（连线即拒绝 + 面板提示，不静默）。
enum class GraphPortType { Float, Vec2, Vec3, Vec4, Sampler2D };

// 端口规格（节点类型表用）
struct GraphPortSpec {
    std::string name;
    GraphPortType type;
};

// 节点类型规格（v1 节点集 17 个：7 输入 + 9 运算 + Master；未知类型 → validateGraph 明确报不支持）
struct GraphNodeSpec {
    std::string type;          // 资产内存储名（"TextureSample"/"Mul"/"Master"...）
    std::string displayName;   // 面板显示名
    std::string category;      // 面板分类："Input"/"Math"/"Master"
    std::vector<GraphPortSpec> inputs;
    std::vector<GraphPortSpec> outputs;
    std::vector<std::string> paramNames;   // 可编辑参数键（值统一字符串，默认值见 defaultParam）
};

// 节点实例（资产数据模型）
struct ShaderGraphNode {
    std::string id;
    std::string type;
    std::array<float, 2> pos = {0.0f, 0.0f};    // 画布坐标（面板用；JSON "pos": [x, y]）
    std::map<std::string, std::string> params;  // 参数值（字符串化；缺省用 defaultParam）
};

// 边实例（JSON "from": [node, port] / "to": [node, port]）
struct ShaderGraphEdge {
    std::string fromNode, fromPort;
    std::string toNode, toPort;
};

class ShaderGraphAsset {
public:
    // AssetStream 读取（pak 兼容）；解析失败/缺 version → nullptr + Logger::warn
    static std::shared_ptr<ShaderGraphAsset> load(const std::filesystem::path& relPath);
    // 写盘（相对路径按 AssetDatabase::projectRoot() 解析；projectRoot 空 → 相对 CWD）
    static bool save(const std::filesystem::path& relPath, const ShaderGraphAsset& graph);

    // 校验：环 / 类型不匹配 / 缺失输入 / 未知类型 / 重复连接 → 错误列表（明确错误文本，
    // 面板直接展示；含节点 id，面板据此高亮）。Master 输入槽未连接不算错（回退材质 uniform，
    // 见 CodeGen 注释）。
    std::vector<std::string> validateGraph() const;

    // ==== 数据模型（cG 扩展，骨架日冻结的 load/save/validateGraph 签名不变）====
    std::string name;                        // 显示名（可选，缺省 = 文件名 stem）
    std::vector<ShaderGraphNode> nodes;
    std::vector<ShaderGraphEdge> edges;

    // 节点类型表（面板分类列表 / 校验 / CodeGen 共用）
    static const GraphNodeSpec* specOf(const std::string& type);
    static const std::vector<GraphNodeSpec>& allSpecs();
    // 新建节点默认参数（paramName 必须在该类型的 paramNames 内）
    static std::string defaultParam(const std::string& type, const std::string& paramName);
    // 端口类型隐式转换判定（见 GraphPortType 注释）
    static bool isConvertible(GraphPortType from, GraphPortType to);
    // 面板辅助：节点查找 / 端口类型查询（空 = 节点或端口不存在）
    const ShaderGraphNode* findNode(const std::string& id) const;
    static bool portExists(const GraphNodeSpec& spec, const std::string& port, bool isInput);

private:
    // 环检测（DFS 着色）；返回环路径文本（空 = 无环）
    std::string findCyclePath() const;
};

}  // namespace NovaEngine
