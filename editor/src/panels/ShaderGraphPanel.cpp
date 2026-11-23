// ShaderGraphPanel.cpp —— Shader Graph 简版面板（V1.0-5，§27.4；cG 独占）
//
// 画布（ImGui 自绘：节点框 + bezier 连线 + 拖拽）+ 节点面板（分类列表点击/拖入画布）
// + 选中节点参数区 + 编译按钮/状态区（错误节点高亮 + 完整 GL log）+ 保存/加载
// .novashadergraph。不 dock 不接线（合波日归主会话）。
// 校验纪律：连线类型不匹配/重复连接/环/未知节点 → 面板状态行明确提示（不静默）。

#include "ShaderGraphPanel.h"

#include "../EditorContext.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/MeshRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <map>
#include <set>

namespace NovaEditor {

namespace {

constexpr const char* kNodeTypePayload = "NOVA_SG_NODE_TYPE";   // 节点面板拖拽 payload
constexpr float kNodeWidth = 180.0f;
constexpr float kHeaderH = 22.0f;
constexpr float kPortRowH = 18.0f;
constexpr float kPortR = 5.0f;
constexpr ImU32 kColNodeBg = IM_COL32(46, 52, 64, 255);
constexpr ImU32 kColNodeTitle = IM_COL32(58, 68, 88, 255);
constexpr ImU32 kColBorder = IM_COL32(120, 130, 150, 255);
constexpr ImU32 kColBorderSel = IM_COL32(255, 170, 60, 255);
constexpr ImU32 kColBorderErr = IM_COL32(230, 60, 60, 255);
constexpr ImU32 kColWire = IM_COL32(180, 190, 210, 255);
constexpr ImU32 kColWireSel = IM_COL32(255, 170, 60, 255);
constexpr ImU32 kColPortIn = IM_COL32(120, 200, 120, 255);
constexpr ImU32 kColPortOut = IM_COL32(200, 150, 120, 255);

// 节点框尺寸（端口行数取输入/输出较多者）
ImVec2 nodeSize(const NovaEngine::GraphNodeSpec& spec) {
    const size_t rows = std::max(spec.inputs.size(), spec.outputs.size());
    return ImVec2(kNodeWidth, kHeaderH + static_cast<float>(rows) * kPortRowH + 10.0f);
}

// 参数编辑种类
enum class ParamKind { String, Float, Vec2, Vec3, Color };

ParamKind paramKind(const std::string& nodeType, const std::string& name) {
    if (name == "path") return ParamKind::String;
    if (name == "color") return ParamKind::Color;
    if (name == "min" || name == "max") return ParamKind::Float;
    if (nodeType == "Vector2") return ParamKind::Vec2;
    if (nodeType == "Vector3") return ParamKind::Vec3;
    return ParamKind::Float;
}

}  // namespace

ShaderGraphPanel::ShaderGraphPanel() : Panel("Shader Graph") {}

std::string ShaderGraphPanel::newId() {
    // 唯一 id（跳过已占用号）
    std::set<std::string> used;
    for (const auto& n : m_graph.nodes) used.insert(n.id);
    std::string id;
    do {
        id = "n" + std::to_string(m_nextId++);
    } while (used.count(id));
    return id;
}

void ShaderGraphPanel::addNodeToGraph(const std::string& type, ImVec2 pos) {
    const NovaEngine::GraphNodeSpec* spec = NovaEngine::ShaderGraphAsset::specOf(type);
    if (!spec) {
        m_status = "未知节点类型 \"" + type + "\"（v1 节点集不支持——明确提示而非静默）";
        return;
    }
    NovaEngine::ShaderGraphNode n;
    n.id = newId();
    n.type = type;
    n.pos = {pos.x, pos.y};
    for (const auto& p : spec->paramNames)
        n.params[p] = NovaEngine::ShaderGraphAsset::defaultParam(type, p);
    m_graph.nodes.push_back(std::move(n));
    m_selectedNode = m_graph.nodes.back().id;
    m_status = "已添加节点 " + m_selectedNode + "（" + spec->displayName + "）";
}

void ShaderGraphPanel::tryConnect(const std::string& fromNode, const std::string& fromPort,
                                  const std::string& toNode, const std::string& toPort) {
    using namespace NovaEngine;
    const ShaderGraphNode* from = m_graph.findNode(fromNode);
    const ShaderGraphNode* to = m_graph.findNode(toNode);
    const GraphNodeSpec* fs = from ? ShaderGraphAsset::specOf(from->type) : nullptr;
    const GraphNodeSpec* ts = to ? ShaderGraphAsset::specOf(to->type) : nullptr;
    if (!from || !to || !fs || !ts) {
        m_status = "连线端点无效（节点或端口不存在）";
        return;
    }
    if (!ShaderGraphAsset::portExists(*fs, fromPort, false) ||
        !ShaderGraphAsset::portExists(*ts, toPort, true)) {
        m_status = "端口不存在: " + fromNode + "." + fromPort + " / " + toNode + "." + toPort;
        return;
    }
    // 重复连接
    for (const auto& e : m_graph.edges)
        if (e.toNode == toNode && e.toPort == toPort) {
            m_status = std::format("输入端口 {}.{} 已被连线占用（v1 每端口只允许一条输入）",
                                   toNode, toPort);
            return;
        }
    // 类型匹配（隐式转换规则：数值互通，sampler2D 孤立）
    auto portType = [](const GraphNodeSpec& spec, const std::string& name, bool in) {
        const auto& list = in ? spec.inputs : spec.outputs;
        for (const auto& p : list)
            if (p.name == name) return p.type;
        return GraphPortType::Float;
    };
    const GraphPortType ft = portType(*fs, fromPort, false);
    const GraphPortType tt = portType(*ts, toPort, true);
    if (!ShaderGraphAsset::isConvertible(ft, tt)) {
        m_status = std::format("连线类型不匹配: {}.{} → {}.{}（sampler2D 与数值类型不可互连）",
                               fromNode, fromPort, toNode, toPort);
        return;
    }
    // 环检测：临时加边 → 全量校验 → 有错回滚（错误文本透传）
    m_graph.edges.push_back({fromNode, fromPort, toNode, toPort});
    const auto errors = m_graph.validateGraph();
    if (!errors.empty()) {
        m_graph.edges.pop_back();
        m_status = "连线被拒绝: " + errors.front();
        return;
    }
    m_status = std::format("已连接 {}.{} → {}.{}（请重新编译生效）", fromNode, fromPort, toNode,
                           toPort);
    m_compiledShader.reset();
    m_nodeErrors.clear();
    m_validationErrors.clear();
    m_errorNodeIds.clear();
}

void ShaderGraphPanel::refreshErrorNodes() {
    m_errorNodeIds.clear();
    for (const auto& e : m_validationErrors)
        for (const auto& n : m_graph.nodes)
            if (e.find(n.id) != std::string::npos) {
                m_errorNodeIds.push_back(n.id);
                break;
            }
    for (const auto& e : m_nodeErrors)
        if (!e.nodeId.empty() && m_graph.findNode(e.nodeId) &&
            std::find(m_errorNodeIds.begin(), m_errorNodeIds.end(), e.nodeId) ==
                m_errorNodeIds.end())
            m_errorNodeIds.push_back(e.nodeId);
}

bool ShaderGraphPanel::isErrorNode(const std::string& id) const {
    return std::find(m_errorNodeIds.begin(), m_errorNodeIds.end(), id) != m_errorNodeIds.end();
}

void ShaderGraphPanel::compile(EditorContext& /*ctx*/) {
    using namespace NovaEngine;
    m_compileLog.clear();
    m_nodeErrors.clear();
    m_validationErrors.clear();
    m_compiledShader.reset();

    std::string fsSrc;
    std::vector<std::string> errors;
    if (!ShaderGraphCodeGen::generate(m_graph, fsSrc, errors)) {
        m_validationErrors = errors;
        m_compileLog = errors.empty() ? "(无错误)" : errors[0];
        for (size_t i = 1; i < errors.size(); ++i) m_compileLog += "\n" + errors[i];
        m_status = std::format("图校验失败（{} 项），未编译——错误节点已高亮", errors.size());
        refreshErrorNodes();
        return;
    }
    std::string log;
    if (ShaderGraphCodeGen::compileGenerated(fsSrc, log)) {
        std::string vsSrc;
        AssetStream::readAllText("assets/shaders/blinn_phong.vert", vsSrc);
        m_compiledShader = Shader::createFromSource(vsSrc, fsSrc);
        if (m_compiledShader) {
            m_status = "编译成功";
            m_compileLog = "编译成功（GLSL 无错误）";
        } else {
            m_status = "链接失败（见 log；Logger 亦已输出）";
            m_compileLog = "链接失败（见 Console 完整 log）\n" + log;
        }
    } else {
        m_nodeErrors = ShaderGraphCodeGen::mapCompileErrors(log);
        m_compileLog = log;
        m_status = std::format("编译失败：{} 处错误（#line 行号已反查节点，红框高亮）",
                               m_nodeErrors.size());
    }
    refreshErrorNodes();
}

void ShaderGraphPanel::onGui(EditorContext& ctx, EditorCamera&) {
    using namespace NovaEngine;
    ImGui::Begin(m_name.c_str(), &open);

    // ===== 工具栏 =====
    if (ImGui::Button("New")) {
        m_graph = ShaderGraphAsset{};
        m_graphPath.clear();
        m_selectedNode.clear();
        m_selectedEdge.clear();
        m_compiledShader.reset();
        m_status = "新图（未保存）";
        m_compileLog.clear();
        m_nextId = 1;
    }
    ImGui::SameLine();
    static std::string pathBuf;
    if (pathBuf.empty()) pathBuf = "assets/shadergraphs/untitled.novashadergraph";
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("##graphPath", &pathBuf);
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        m_graphPath = std::filesystem::path(pathBuf);
        if (const auto g = ShaderGraphAsset::load(m_graphPath)) {
            m_graph = *g;
            m_selectedNode.clear();
            m_selectedEdge.clear();
            m_compiledShader.reset();
            m_status = "已加载: " + pathBuf;
            m_compileLog.clear();
            m_nextId = 1;
            for (const auto& n : m_graph.nodes) {   // id 序列续接（防重号）
                const std::string s = n.id;
                size_t d = s.find_first_of("0123456789");
                if (d != std::string::npos) {
                    try {
                        m_nextId = std::max<uint64_t>(m_nextId,
                                                      std::stoull(s.substr(d)) + 1);
                    } catch (...) {
                    }
                }
            }
        } else {
            m_status = "加载失败: " + pathBuf + "（JSON/version 错误，详见日志）";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        m_graphPath = std::filesystem::path(pathBuf);
        m_status = ShaderGraphAsset::save(m_graphPath, m_graph)
                       ? "已保存: " + pathBuf
                       : "保存失败: " + pathBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("Compile")) compile(ctx);
    ImGui::SameLine();
    if (ImGui::Button("Apply to Selection")) {
        if (!m_compiledShader || m_graphPath.empty()) {
            m_status = "请先保存并编译成功，再应用到选中对象";
        } else if (GameObject* go = ctx.activeObject()) {
            if (MeshRenderer* mr = go->getComponent<MeshRenderer>()) {
                mr->material.shaderOverride = m_compiledShader;
                // 序列化用相对路径（与 Inspector 合波接线同契约，§27.4）
                std::error_code ec;
                const auto rel = std::filesystem::relative(m_graphPath,
                                                           AssetDatabase::projectRoot(), ec);
                mr->material.shaderGraphPath =
                    (ec || rel.empty()) ? m_graphPath.generic_string() : rel.generic_string();
                ctx.markSceneDirty();
                m_status = "已应用 shader 到选中对象（shaderGraphPath 已写序列化字段）";
            } else {
                m_status = "选中对象无 MeshRenderer";
            }
        } else {
            m_status = "未选中对象";
        }
    }
    ImGui::TextUnformatted(m_status.c_str());

    // ===== 三栏：节点面板 | 画布 | 参数 =====
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float paletteW = 170.0f, paramsW = 220.0f;
    const float canvasW = std::max(200.0f, avail.x - paletteW - paramsW);
    const float bodyH = std::max(120.0f, avail.y - 120.0f);   // 底部留错误 log 区

    // ---- 节点面板（分类列表；点击/拖入画布）----
    ImGui::BeginChild("sg_palette", ImVec2(paletteW, bodyH), true);
    ImGui::TextUnformatted("Nodes");
    ImGui::Separator();
    std::string lastCategory;
    for (const auto& spec : ShaderGraphAsset::allSpecs()) {
        if (spec.category != lastCategory) {
            if (!lastCategory.empty()) ImGui::Separator();
            ImGui::TextDisabled("%s", spec.category.c_str());
            lastCategory = spec.category;
        }
        const std::string label = spec.displayName + "##pal_" + spec.type;
        if (ImGui::Button(label.c_str(), ImVec2(paletteW - 20.0f, 0.0f)))
            addNodeToGraph(spec.type, ImVec2(30.0f, 30.0f + m_graph.nodes.size() * 30.0f));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kNodeTypePayload, spec.type.c_str(),
                                      spec.type.size() + 1);
            ImGui::TextUnformatted(spec.displayName.c_str());
            ImGui::EndDragDropSource();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- 画布 ----
    ImGui::BeginChild("sg_canvas", ImVec2(canvasW, bodyH), true);
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasMax = ImVec2(canvasMin.x + canvasW, canvasMin.y + bodyH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inCanvas = ImGui::IsWindowHovered() || ImGui::IsWindowFocused();
    const bool mouseInCanvas = mouse.x >= canvasMin.x && mouse.x <= canvasMax.x &&
                               mouse.y >= canvasMin.y && mouse.y <= canvasMax.y;

    // 节点拖入（palette 拖拽）
    if (inCanvas && mouseInCanvas)
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kNodeTypePayload)) {
            const std::string type(static_cast<const char*>(p->Data));
            addNodeToGraph(type, ImVec2(mouse.x - canvasMin.x - 40.0f,
                                        mouse.y - canvasMin.y - kHeaderH * 0.5f));
        }

    // 节点布局（端口位置缓存：nodeId → 输入/输出端口屏幕坐标）
    struct PortPos {
        ImVec2 p;
        std::string port;
    };
    std::map<std::string, std::vector<PortPos>> inPorts, outPorts;
    std::map<std::string, ImVec2> nodeMin, nodeMax;

    for (const auto& n : m_graph.nodes) {
        const GraphNodeSpec* spec = ShaderGraphAsset::specOf(n.type);
        if (!spec) continue;
        const ImVec2 size = nodeSize(*spec);
        const ImVec2 mn = ImVec2(canvasMin.x + n.pos[0], canvasMin.y + n.pos[1]);
        const ImVec2 mx = ImVec2(mn.x + size.x, mn.y + size.y);
        nodeMin[n.id] = mn;
        nodeMax[n.id] = mx;
        for (size_t i = 0; i < spec->inputs.size(); ++i)
            inPorts[n.id].push_back(
                {ImVec2(mn.x, mn.y + kHeaderH + 6.0f + (static_cast<float>(i) + 0.5f) * kPortRowH),
                 spec->inputs[i].name});
        for (size_t i = 0; i < spec->outputs.size(); ++i)
            outPorts[n.id].push_back(
                {ImVec2(mx.x, mn.y + kHeaderH + 6.0f + (static_cast<float>(i) + 0.5f) * kPortRowH),
                 spec->outputs[i].name});
    }

    // ---- 交互：节点拖拽 / 选中 ----
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInCanvas) {
        bool hitNode = false;
        for (const auto& n : m_graph.nodes) {
            const ImVec2 mn = nodeMin[n.id], mx = nodeMax[n.id];
            if (mouse.x < mn.x || mouse.x > mx.x || mouse.y < mn.y || mouse.y > mx.y) continue;
            if (mouse.y <= mn.y + kHeaderH) {   // 标题栏 → 开始拖拽
                m_dragNode = n.id;
                m_dragOffset = ImVec2(mouse.x - mn.x, mouse.y - mn.y);
            }
            m_selectedNode = n.id;
            m_selectedEdge.clear();
            hitNode = true;
            break;
        }
        if (!hitNode && ImGui::IsWindowHovered()) {   // 点空白 → 清选中
            m_selectedNode.clear();
            m_selectedEdge.clear();
        }
    }
    if (!m_dragNode.empty()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            for (auto& n : m_graph.nodes)
                if (n.id == m_dragNode) {
                    n.pos[0] = std::max(0.0f, mouse.x - canvasMin.x - m_dragOffset.x);
                    n.pos[1] = std::max(0.0f, mouse.y - canvasMin.y - m_dragOffset.y);
                }
        } else {
            m_dragNode.clear();
        }
    }

    // ---- 连线：输出端口按下 → 拖线 → 输入端口松开 ----
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInCanvas) {
        for (const auto& [id, ports] : outPorts)
            for (const auto& pp : ports)
                if (std::hypot(mouse.x - pp.p.x, mouse.y - pp.p.y) <= kPortR + 3.0f) {
                    m_dragLink = id + ":" + pp.port;
                    break;
                }
    }
    if (!m_dragLink.empty() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        bool connected = false;
        for (const auto& [id, ports] : inPorts)
            for (const auto& pp : ports)
                if (std::hypot(mouse.x - pp.p.x, mouse.y - pp.p.y) <= kPortR + 3.0f) {
                    const auto sep = m_dragLink.find(':');
                    tryConnect(m_dragLink.substr(0, sep), m_dragLink.substr(sep + 1), id,
                               pp.port);
                    connected = true;
                    break;
                }
        if (!connected) m_status = "连线已取消（未落在输入端口上）";
        m_dragLink.clear();
    }

    // ---- 边选中（bezier 采样命中）----
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInCanvas && m_dragLink.empty()) {
        for (const auto& e : m_graph.edges) {
            const auto oi = outPorts.find(e.fromNode);
            const auto ii = inPorts.find(e.toNode);
            if (oi == outPorts.end() || ii == inPorts.end()) continue;
            const ImVec2* p0 = nullptr;
            const ImVec2* p3 = nullptr;
            for (const auto& pp : oi->second)
                if (pp.port == e.fromPort) p0 = &pp.p;
            for (const auto& pp : ii->second)
                if (pp.port == e.toPort) p3 = &pp.p;
            if (!p0 || !p3) continue;
            const ImVec2 c1(p0->x + 60.0f, p0->y), c2(p3->x - 60.0f, p3->y);
            for (int t = 0; t <= 20; ++t) {
                const float u = static_cast<float>(t) / 20.0f;
                const float v = 1.0f - u;
                const ImVec2 p = ImVec2(v * v * v * p0->x + 3 * v * v * u * c1.x +
                                            3 * v * u * u * c2.x + u * u * u * p3->x,
                                        v * v * v * p0->y + 3 * v * v * u * c1.y +
                                            3 * v * u * u * c2.y + u * u * u * p3->y);
                if (std::hypot(mouse.x - p.x, mouse.y - p.y) <= 6.0f) {
                    m_selectedEdge = e.fromNode + ":" + e.fromPort + ">" + e.toNode + ":" + e.toPort;
                    m_selectedNode.clear();
                    break;
                }
            }
            if (!m_selectedEdge.empty()) break;
        }
    }

    // ---- 删除（Del：节点连带边 / 单边；文本框编辑中不触发——防路径/参数输入时误删）----
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && ImGui::IsWindowFocused() &&
        !ImGui::IsAnyItemActive()) {
        if (!m_selectedNode.empty()) {
            m_graph.nodes.erase(std::remove_if(m_graph.nodes.begin(), m_graph.nodes.end(),
                                               [&](const ShaderGraphNode& n) {
                                                   return n.id == m_selectedNode;
                                               }),
                                m_graph.nodes.end());
            m_graph.edges.erase(std::remove_if(m_graph.edges.begin(), m_graph.edges.end(),
                                               [&](const ShaderGraphEdge& e) {
                                                   return e.fromNode == m_selectedNode ||
                                                          e.toNode == m_selectedNode;
                                               }),
                                m_graph.edges.end());
            m_status = "已删除节点 " + m_selectedNode + "（连带其连线）";
            m_selectedNode.clear();
        } else if (!m_selectedEdge.empty()) {
            const auto gt = m_selectedEdge.find('>');
            const std::string from = m_selectedEdge.substr(0, gt);
            const std::string to = m_selectedEdge.substr(gt + 1);
            const auto fc = from.find(':');
            const auto tc = to.find(':');
            m_graph.edges.erase(std::remove_if(m_graph.edges.begin(), m_graph.edges.end(),
                                               [&](const ShaderGraphEdge& e) {
                                                   return e.fromNode == from.substr(0, fc) &&
                                                          e.fromPort == from.substr(fc + 1) &&
                                                          e.toNode == to.substr(0, tc) &&
                                                          e.toPort == to.substr(tc + 1);
                                               }),
                                m_graph.edges.end());
            m_status = "已删除连线";
            m_selectedEdge.clear();
        }
        m_compiledShader.reset();
    }

    // ---- 绘制：边 ----
    for (const auto& e : m_graph.edges) {
        const auto oi = outPorts.find(e.fromNode);
        const auto ii = inPorts.find(e.toNode);
        if (oi == outPorts.end() || ii == inPorts.end()) continue;
        ImVec2 p0{}, p3{};
        bool ok = false;
        for (const auto& pp : oi->second)
            if (pp.port == e.fromPort) {
                p0 = pp.p;
                ok = true;
            }
        for (const auto& pp : ii->second)
            if (pp.port == e.toPort) p3 = pp.p;
        if (!ok) continue;
        const std::string key =
            e.fromNode + ":" + e.fromPort + ">" + e.toNode + ":" + e.toPort;
        dl->AddBezierCubic(p0, ImVec2(p0.x + 60.0f, p0.y), ImVec2(p3.x - 60.0f, p3.y), p3,
                           key == m_selectedEdge ? kColWireSel : kColWire, 2.0f);
    }

    // ---- 绘制：节点 ----
    for (const auto& n : m_graph.nodes) {
        const GraphNodeSpec* spec = ShaderGraphAsset::specOf(n.type);
        if (!spec) continue;
        const ImVec2 mn = nodeMin[n.id], mx = nodeMax[n.id];
        const bool sel = n.id == m_selectedNode;
        const bool err = isErrorNode(n.id);
        dl->AddRectFilled(mn, mx, kColNodeBg, 4.0f);
        dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + kHeaderH), kColNodeTitle, 4.0f);
        dl->AddRect(mn, mx, err ? kColBorderErr : (sel ? kColBorderSel : kColBorder), 4.0f,
                    err ? 2.0f : 1.5f);
        dl->AddText(ImVec2(mn.x + 6.0f, mn.y + 4.0f), IM_COL32(230, 235, 245, 255),
                    (spec->displayName + "  (" + n.id + ")").c_str());
        // 端口
        for (const auto& pp : inPorts[n.id]) {
            dl->AddCircleFilled(pp.p, kPortR, kColPortIn);
            dl->AddText(ImVec2(pp.p.x + 8.0f, pp.p.y - 8.0f), IM_COL32(200, 205, 215, 255),
                        pp.port.c_str());
        }
        for (const auto& pp : outPorts[n.id]) {
            dl->AddCircleFilled(pp.p, kPortR, kColPortOut);
            const ImVec2 ts = ImGui::CalcTextSize(pp.port.c_str());
            dl->AddText(ImVec2(pp.p.x - 8.0f - ts.x, pp.p.y - 8.0f), IM_COL32(200, 205, 215, 255),
                        pp.port.c_str());
        }
    }
    // 拖线中的虚线
    if (!m_dragLink.empty()) {
        const auto sep = m_dragLink.find(':');
        const auto oi = outPorts.find(m_dragLink.substr(0, sep));
        if (oi != outPorts.end())
            for (const auto& pp : oi->second)
                if (pp.port == m_dragLink.substr(sep + 1))
                    dl->AddBezierCubic(pp.p, ImVec2(pp.p.x + 60.0f, pp.p.y),
                                       ImVec2(mouse.x - 60.0f, mouse.y), mouse, kColWireSel, 2.0f);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- 选中节点参数区 ----
    ImGui::BeginChild("sg_params", ImVec2(paramsW, bodyH), true);
    ImGui::TextUnformatted("Params");
    ImGui::Separator();
    const ShaderGraphNode* selNode = m_graph.findNode(m_selectedNode);
    if (!selNode) {
        ImGui::TextDisabled("(未选中节点)");
    } else {
        const GraphNodeSpec* spec = ShaderGraphAsset::specOf(selNode->type);
        if (!spec || spec->paramNames.empty()) {
            ImGui::TextDisabled("(该节点无参数)");
        } else {
            ImGui::TextUnformatted(spec->displayName.c_str());
            ShaderGraphNode* mut = nullptr;   // 可变访问（编辑写回）
            for (auto& n : m_graph.nodes)
                if (n.id == m_selectedNode) mut = &n;
            bool edited = false;
            for (const auto& pn : spec->paramNames) {
                std::string& val = mut->params[pn];
                const ParamKind kind = paramKind(selNode->type, pn);
                ImGui::PushID(pn.c_str());
                if (kind == ParamKind::String) {
                    edited |= ImGui::InputText(pn.c_str(), &val);
                } else if (kind == ParamKind::Color) {
                    float c[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    sscanf_s(val.c_str(), "%f %f %f %f", &c[0], &c[1], &c[2], &c[3]);
                    if (ImGui::ColorEdit4(pn.c_str(), c)) {
                        val = std::format("{} {} {} {}", c[0], c[1], c[2], c[3]);
                        edited = true;
                    }
                } else if (kind == ParamKind::Vec2) {
                    float v[2] = {0.0f, 0.0f};
                    sscanf_s(val.c_str(), "%f %f", &v[0], &v[1]);
                    if (ImGui::DragFloat2(pn.c_str(), v, 0.01f)) {
                        val = std::format("{} {}", v[0], v[1]);
                        edited = true;
                    }
                } else if (kind == ParamKind::Vec3) {
                    float v[3] = {0.0f, 0.0f, 0.0f};
                    sscanf_s(val.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
                    if (ImGui::DragFloat3(pn.c_str(), v, 0.01f)) {
                        val = std::format("{} {} {}", v[0], v[1], v[2]);
                        edited = true;
                    }
                } else {
                    float v = 0.0f;
                    try {
                        v = std::stof(val);
                    } catch (...) {
                    }
                    if (ImGui::DragFloat(pn.c_str(), &v, 0.01f)) {
                        val = std::format("{}", v);
                        edited = true;
                    }
                }
                ImGui::PopID();
            }
            if (edited) {
                m_status = "参数已改动（请重新编译/应用）";
                m_compiledShader.reset();
                m_nodeErrors.clear();
                m_validationErrors.clear();
                m_errorNodeIds.clear();
            }
        }
        // Master 槽连接状态提示
        if (selNode->type == "Master") {
            ImGui::Separator();
            ImGui::TextDisabled("(未连接的槽回退材质 uniform)");
        }
    }
    ImGui::EndChild();

    // ===== 编译 log / 错误区 =====
    ImGui::Separator();
    if (!m_compileLog.empty()) {
        if (ImGui::CollapsingHeader("Compile Log", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("sg_log", ImVec2(0, 90.0f), true);
            ImGui::TextUnformatted(m_compileLog.c_str());
            ImGui::EndChild();
        }
    }
    for (const auto& e : m_nodeErrors) {   // 错误行（点击选中对应节点）
        if (!e.nodeId.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[line %d, node %s]",
                               e.line, e.nodeId.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(("select##" + e.nodeId).c_str())) {
                m_selectedNode = e.nodeId;
                m_selectedEdge.clear();
            }
        }
    }
    ImGui::End();
}

}  // namespace NovaEditor
