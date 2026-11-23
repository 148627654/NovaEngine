#pragma once
#include "../Panel.h"

#include <imgui.h>   // ImVec2（画布交互状态成员）

#include <NovaEngine/ShaderGraph/ShaderGraphAsset.h>
#include <NovaEngine/ShaderGraph/ShaderGraphCodeGen.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace NovaEngine {
class Shader;
}  // namespace NovaEngine

namespace NovaEditor {

// Shader Graph 面板（V1.0-5，§27.4；本文件 cG 独占）：
// 画布（ImGui 自绘：节点框 + bezier 连线 + 拖拽）+ 节点面板（分类列表，点击/拖入画布）
// + 选中节点参数区 + 编译按钮/状态区（错误节点高亮 + 完整 GL log）+ 保存/加载
// .novashadergraph（assets/shadergraphs/）。
// 不 dock 不接线（合波日归主会话）；「Apply to Selection」把编译结果赋给选中对象的
// MeshRenderer（shaderOverride + shaderGraphPath，与 Inspector 合波接线同契约）。
class ShaderGraphPanel : public Panel {
public:
    explicit ShaderGraphPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    NovaEngine::ShaderGraphAsset m_graph;          // 当前编辑的图
    std::filesystem::path m_graphPath;             // 资产路径（保存/加载）
    std::string m_status;                          // 状态行（校验/编译结果摘要）
    std::string m_compileLog;                      // 最近一次完整编译 log（成功 = "编译成功"）
    std::vector<NovaEngine::ShaderGraphCodeGen::GraphNodeError> m_nodeErrors;
    std::vector<std::string> m_validationErrors;   // 图校验错误（含节点 id 文本）
    std::vector<std::string> m_errorNodeIds;       // 错误节点 id（画布红框高亮）
    std::shared_ptr<NovaEngine::Shader> m_compiledShader;   // 最近一次编译成功产物
    uint64_t m_nextId = 1;                         // 新节点 id 序列（n1, n2, ...）

    // ===== 画布交互状态 =====
    std::string m_selectedNode;                    // 选中节点 id（空 = 无）
    std::string m_selectedEdge;                    // 选中边 key
    std::string m_dragNode;                        // 拖动中的节点 id
    std::string m_dragLink;                        // 拖线中的源 "nodeId:port"（输出端口）
    ImVec2 m_dragOffset{0.0f, 0.0f};               // 拖动节点时鼠标与节点原点的偏移

    // 编译（generate + compileGenerated + Shader::createFromSource）；刷新错误/状态
    void compile(EditorContext& ctx);
    // 校验错误 → 节点高亮集合（错误文本内扫描图节点 id）
    void refreshErrorNodes();
    bool isErrorNode(const std::string& id) const;   // 画布高亮判定
    std::string newId();                           // 生成唯一节点 id
    // 连线尝试：类型/重复/环校验失败 → 状态提示并拒绝（明确提示而非静默）
    void tryConnect(const std::string& fromNode, const std::string& fromPort,
                    const std::string& toNode, const std::string& toPort);
    void addNodeToGraph(const std::string& type, ImVec2 pos);
};

}  // namespace NovaEditor
