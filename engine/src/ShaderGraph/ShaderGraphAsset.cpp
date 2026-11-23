// ===== V1.0-5 ShaderGraphAsset（§27.4）真实实现（cG，2026-11-23）=====
// 节点/边数据模型 + 节点类型表（v1 17 个）+ JSON 往返（version 1，AssetStream 读取）
// + 校验（环/类型不匹配/缺失输入/未知类型/重复连接 → 明确错误文本）。
// params 值统一字符串存储 → JSON 往返无损（§27.4 验收 3）。

#include <NovaEngine/ShaderGraph/ShaderGraphAsset.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace NovaEngine {

namespace {

// ===== v1 节点类型表（§27.4 节点集：7 输入 + 9 运算 + Master）=====
// 端口类型选择：运算类定类型端口（隐式转换规则负责 splat/截断互通，见头文件注释）；
// Mul 定 vec4——验收图「Mul(Color, TextureSample)→albedo」逐字可连。
const std::vector<GraphNodeSpec> kSpecs = {
    // ---- Input ----
    {"UV", "UV", "Input", {}, {{"uv", GraphPortType::Vec2}}, {}},
    {"Time", "Time", "Input", {}, {{"t", GraphPortType::Float}}, {}},
    {"Color", "Color", "Input", {}, {{"rgba", GraphPortType::Vec4}}, {"color"}},
    {"Scalar", "Scalar", "Input", {}, {{"x", GraphPortType::Float}}, {"value"}},
    {"Vector2", "Vector2", "Input", {}, {{"v", GraphPortType::Vec2}}, {"value"}},
    {"Vector3", "Vector3", "Input", {}, {{"v", GraphPortType::Vec3}}, {"value"}},
    // tex（sampler2D）端口存在意义：§27.4 端口类型体系含 sampler2D——v1 节点集中没有任何
    // 输入端口接受 sampler2D，连线即触发「类型不匹配拒绝」路径（可演示，不静默）
    {"TextureSample", "Texture Sample", "Input", {},
     {{"rgba", GraphPortType::Vec4}, {"tex", GraphPortType::Sampler2D}}, {"path"}},
    // ---- Math ----
    {"Add", "Add", "Math",
     {{"a", GraphPortType::Vec3}, {"b", GraphPortType::Vec3}},
     {{"sum", GraphPortType::Vec3}}, {}},
    {"Sub", "Subtract", "Math",
     {{"a", GraphPortType::Vec3}, {"b", GraphPortType::Vec3}},
     {{"diff", GraphPortType::Vec3}}, {}},
    {"Mul", "Multiply", "Math",
     {{"a", GraphPortType::Vec4}, {"b", GraphPortType::Vec4}},
     {{"prod", GraphPortType::Vec4}}, {}},
    {"Div", "Divide", "Math",
     {{"a", GraphPortType::Vec3}, {"b", GraphPortType::Vec3}},
     {{"quot", GraphPortType::Vec3}}, {}},
    {"Lerp", "Lerp", "Math",
     {{"a", GraphPortType::Vec3}, {"b", GraphPortType::Vec3}, {"t", GraphPortType::Float}},
     {{"out", GraphPortType::Vec3}}, {}},
    {"Clamp", "Clamp", "Math",
     {{"x", GraphPortType::Float}},
     {{"out", GraphPortType::Float}}, {"min", "max"}},
    {"OneMinus", "One Minus", "Math",
     {{"x", GraphPortType::Vec3}},
     {{"out", GraphPortType::Vec3}}, {}},
    {"Normalize", "Normalize", "Math",
     {{"x", GraphPortType::Vec3}},
     {{"out", GraphPortType::Vec3}}, {}},
    {"Dot", "Dot Product", "Math",
     {{"a", GraphPortType::Vec3}, {"b", GraphPortType::Vec3}},
     {{"dot", GraphPortType::Float}}, {}},
    // ---- Master ----
    {"Master", "Master Output", "Master",
     {{"albedo", GraphPortType::Vec3}, {"roughness", GraphPortType::Float},
      {"metallic", GraphPortType::Float}, {"normal", GraphPortType::Vec3},
      {"emission", GraphPortType::Vec3}},
     {}, {}},
};

// 新节点默认参数（paramName 缺省值；颜色/向量/标量均为字符串）
const std::map<std::pair<std::string, std::string>, std::string> kDefaultParams = {
    {{"Color", "color"}, "1.0 1.0 1.0 1.0"},
    {{"Scalar", "value"}, "0.5"},
    {{"Vector2", "value"}, "0.0 0.0"},
    {{"Vector3", "value"}, "0.0 0.0 0.0"},
    {{"TextureSample", "path"}, ""},
    {{"Clamp", "min"}, "0.0"},
    {{"Clamp", "max"}, "1.0"},
};

std::string portTypeName(GraphPortType t) {
    switch (t) {
        case GraphPortType::Float: return "float";
        case GraphPortType::Vec2: return "vec2";
        case GraphPortType::Vec3: return "vec3";
        case GraphPortType::Vec4: return "vec4";
        case GraphPortType::Sampler2D: return "sampler2D";
    }
    return "?";
}

// 字符串参数安全转换（json 里数字/布尔等形态一律转字符串，保证往返无损与面板可编辑）
std::string paramToString(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_float()) return std::format("{}", v.get<double>());
    if (v.is_number_integer() || v.is_number_unsigned()) return std::to_string(v.get<int64_t>());
    if (v.is_boolean()) return v.get<bool>() ? "1" : "0";
    return v.dump();
}

}  // namespace

// ==================== 静态节点类型表 ====================

const GraphNodeSpec* ShaderGraphAsset::specOf(const std::string& type) {
    for (const auto& s : kSpecs)
        if (s.type == type) return &s;
    return nullptr;
}

const std::vector<GraphNodeSpec>& ShaderGraphAsset::allSpecs() {
    return kSpecs;
}

std::string ShaderGraphAsset::defaultParam(const std::string& type,
                                           const std::string& paramName) {
    const auto it = kDefaultParams.find({type, paramName});
    return it != kDefaultParams.end() ? it->second : std::string{};
}

bool ShaderGraphAsset::isConvertible(GraphPortType from, GraphPortType to) {
    if (from == to) return true;
    if (from == GraphPortType::Sampler2D || to == GraphPortType::Sampler2D) return false;
    return true;   // 数值类型间互通（splat/截断，GLSL 构造语义）
}

const ShaderGraphNode* ShaderGraphAsset::findNode(const std::string& id) const {
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

bool ShaderGraphAsset::portExists(const GraphNodeSpec& spec, const std::string& port,
                                  bool isInput) {
    const auto& list = isInput ? spec.inputs : spec.outputs;
    return std::any_of(list.begin(), list.end(),
                       [&](const GraphPortSpec& p) { return p.name == port; });
}

// ==================== 环检测（DFS 三色标记）====================

std::string ShaderGraphAsset::findCyclePath() const {
    // 邻接表：fromNode → (toNode, fromPort, toPort)
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> adj;
    for (const auto& e : edges)
        adj[e.fromNode].push_back({e.toNode, e.toPort});
    enum class Color { White, Gray, Black };
    std::map<std::string, Color> color;
    std::vector<std::pair<std::string, std::string>> stack;   // (node, 进入端口)
    // 返回路径文本（n1 → n2 → ... → n1）；空 = 无环
    std::string result;
    std::function<bool(const std::string&)> dfs = [&](const std::string& u) -> bool {
        color[u] = Color::Gray;
        const auto it = adj.find(u);
        if (it != adj.end()) {
            for (const auto& [v, port] : it->second) {
                if (color[v] == Color::Gray) {
                    // 找到环：栈内从 v 开始拼路径
                    std::string path = v;
                    for (size_t i = 0; i < stack.size(); ++i) {
                        if (stack[i].first == v) {
                            for (size_t j = i + 1; j < stack.size(); ++j)
                                path += " → " + stack[j].first;
                            break;
                        }
                    }
                    result = path + " → " + v;
                    return true;
                }
                if (color[v] == Color::White) {
                    stack.push_back({v, port});
                    if (dfs(v)) return true;
                    stack.pop_back();
                }
            }
        }
        color[u] = Color::Black;
        return false;
    };
    for (const auto& n : nodes) {
        if (color[n.id] == Color::White) {
            stack.clear();
            stack.push_back({n.id, ""});
            if (dfs(n.id)) return result;
        }
    }
    return {};
}

// ==================== 校验 ====================

std::vector<std::string> ShaderGraphAsset::validateGraph() const {
    std::vector<std::string> errors;

    // 1. Master 存在且唯一
    int masterCount = 0;
    for (const auto& n : nodes)
        if (n.type == "Master") ++masterCount;
    if (masterCount == 0)
        errors.push_back("图缺少 Master 节点（主输出）；请从节点面板添加 Master");
    if (masterCount > 1)
        errors.push_back(std::format("图中存在 {} 个 Master 节点（只允许 1 个）", masterCount));

    // 2. 节点 id 唯一 + 类型受支持
    std::set<std::string> ids;
    for (const auto& n : nodes) {
        if (!ids.insert(n.id).second) {
            errors.push_back(std::format("节点 id 重复: \"{}\"", n.id));
            continue;
        }
        if (!specOf(n.type))
            errors.push_back(std::format("节点 \"{}\" 类型 \"{}\" 不受支持（v1 节点集之外，"
                                         "明确提示而非静默错误）", n.id, n.type));
    }

    // 3. 边端点/端口/类型 + 输入端口重复连接
    std::set<std::pair<std::string, std::string>> connectedInputs;
    for (const auto& e : edges) {
        const ShaderGraphNode* from = findNode(e.fromNode);
        const ShaderGraphNode* to = findNode(e.toNode);
        if (!from || !to) {
            errors.push_back(std::format("边端点节点不存在: \"{}\" → \"{}\"", e.fromNode,
                                         e.toNode));
            continue;
        }
        const GraphNodeSpec* fromSpec = specOf(from->type);
        const GraphNodeSpec* toSpec = specOf(to->type);
        if (!fromSpec || !toSpec) continue;   // 未知类型已在上一步报错
        if (!portExists(*fromSpec, e.fromPort, false)) {
            errors.push_back(std::format("节点 \"{}\"（{}）没有输出端口 \"{}\"", e.fromNode,
                                         from->type, e.fromPort));
            continue;
        }
        if (!portExists(*toSpec, e.toPort, true)) {
            errors.push_back(std::format("节点 \"{}\"（{}）没有输入端口 \"{}\"", e.toNode,
                                         to->type, e.toPort));
            continue;
        }
        if (!connectedInputs.insert({e.toNode, e.toPort}).second) {
            errors.push_back(std::format("输入端口 \"{}.{}\" 被多条连线占用（v1 每端口只允许"
                                         "一条输入）", e.toNode, e.toPort));
        }
        // 类型匹配（隐式转换规则）
        auto portType = [&](const GraphNodeSpec& spec, const std::string& name, bool isInput) {
            const auto& list = isInput ? spec.inputs : spec.outputs;
            for (const auto& p : list)
                if (p.name == name) return p.type;
            return GraphPortType::Float;   // 不可达（portExists 已保证）
        };
        const GraphPortType fromType = portType(*fromSpec, e.fromPort, false);
        const GraphPortType toType = portType(*toSpec, e.toPort, true);
        if (!isConvertible(fromType, toType)) {
            errors.push_back(std::format("连线类型不匹配: \"{}.{}\"({}) → \"{}.{}\"({})"
                                         "（sampler2D 与数值类型不可互连）",
                                         e.fromNode, e.fromPort, portTypeName(fromType),
                                         e.toNode, e.toPort, portTypeName(toType)));
        }
    }

    // 4. 缺失输入（非 Master 节点的输入端口必须全部连接；Master 槽缺省回退材质 uniform）
    for (const auto& n : nodes) {
        if (n.type == "Master") continue;
        const GraphNodeSpec* spec = specOf(n.type);
        if (!spec) continue;
        for (const auto& in : spec->inputs) {
            if (!connectedInputs.count({n.id, in.name}))
                errors.push_back(std::format("节点 \"{}\"（{}）输入 \"{}\" 未连接"
                                             "（v1 节点输入必须连线）", n.id, n.type, in.name));
        }
    }

    // 5. 环
    const std::string cycle = findCyclePath();
    if (!cycle.empty())
        errors.push_back("检测到环（循环依赖）: " + cycle);

    return errors;
}

// ==================== JSON 往返（version 1，§27.4 格式逐字）====================

std::shared_ptr<ShaderGraphAsset> ShaderGraphAsset::load(const std::filesystem::path& relPath) {
    std::string text;
    if (!AssetStream::readAllText(relPath, text)) {
        Logger::warn("[ShaderGraphAsset] 无法读取图资产: {}", relPath.string());
        return nullptr;
    }
    // P2-6（Reviewer）：损坏文件不崩溃——解析 + 字段类型错误（version 非整数 / pos 为字符串 /
    // id/type 类型不符等 nlohmann type_error）统一兜底：warn + nullptr（调用方回退默认 shader）
    try {
        nlohmann::json j = nlohmann::json::parse(text);
        if (!j.contains("version") || j["version"].get<int>() != 1) {
            Logger::warn("[ShaderGraphAsset] 图资产 version 不支持（需 1）: {}", relPath.string());
            return nullptr;
        }
        auto graph = std::make_shared<ShaderGraphAsset>();
        graph->name = relPath.filename().stem().string();
        if (j.contains("nodes") && j["nodes"].is_array()) {
            for (const auto& nj : j["nodes"]) {
                ShaderGraphNode n;
                n.id = nj.value("id", std::string{});
                n.type = nj.value("type", std::string{});
                if (nj.contains("pos") && nj["pos"].is_array() && nj["pos"].size() >= 2) {
                    n.pos[0] = nj["pos"][0].get<float>();
                    n.pos[1] = nj["pos"][1].get<float>();
                }
                if (nj.contains("params") && nj["params"].is_object())
                    for (auto it = nj["params"].begin(); it != nj["params"].end(); ++it)
                        n.params[it.key()] = paramToString(it.value());
                graph->nodes.push_back(std::move(n));
            }
        }
        if (j.contains("edges") && j["edges"].is_array()) {
            for (const auto& ej : j["edges"]) {
                ShaderGraphEdge e;
                if (ej.contains("from") && ej["from"].is_array() && ej["from"].size() >= 2) {
                    e.fromNode = ej["from"][0].get<std::string>();
                    e.fromPort = ej["from"][1].get<std::string>();
                }
                if (ej.contains("to") && ej["to"].is_array() && ej["to"].size() >= 2) {
                    e.toNode = ej["to"][0].get<std::string>();
                    e.toPort = ej["to"][1].get<std::string>();
                }
                graph->edges.push_back(std::move(e));
            }
        }
        return graph;
    } catch (const nlohmann::json::exception& e) {
        Logger::warn("[ShaderGraphAsset] 图资产 JSON 类型错误（不崩溃）: {}（{}）",
                     relPath.string(), e.what());
        return nullptr;
    } catch (const std::exception& e) {
        Logger::warn("[ShaderGraphAsset] 图资产解析失败（不崩溃）: {}（{}）", relPath.string(),
                     e.what());
        return nullptr;
    }
}

bool ShaderGraphAsset::save(const std::filesystem::path& relPath,
                            const ShaderGraphAsset& graph) {
    nlohmann::json j;
    j["version"] = 1;
    j["nodes"] = nlohmann::json::array();
    for (const auto& n : graph.nodes) {
        nlohmann::json nj;
        nj["id"] = n.id;
        nj["type"] = n.type;
        nj["pos"] = {n.pos[0], n.pos[1]};
        nj["params"] = nlohmann::json::object();
        for (const auto& [k, v] : n.params)
            nj["params"][k] = v;   // 字符串参数（往返无损）
        j["nodes"].push_back(std::move(nj));
    }
    j["edges"] = nlohmann::json::array();
    for (const auto& e : graph.edges) {
        nlohmann::json ej;
        ej["from"] = {e.fromNode, e.fromPort};
        ej["to"] = {e.toNode, e.toPort};
        j["edges"].push_back(std::move(ej));
    }

    // 写盘路径：相对路径按项目根解析（projectRoot 空 → 相对 CWD，编辑器运行目录 = 项目根）
    std::filesystem::path full = relPath;
    if (full.is_relative() && !AssetDatabase::projectRoot().empty())
        full = AssetDatabase::projectRoot() / full;
    std::error_code ec;
    std::filesystem::create_directories(full.parent_path(), ec);
    std::ofstream out(full, std::ios::binary);
    if (!out) {
        Logger::warn("[ShaderGraphAsset] 无法写入图资产: {}", full.string());
        return false;
    }
    out << j.dump(2) << "\n";
    return static_cast<bool>(out);
}

}  // namespace NovaEngine
