#pragma once
#include "../Panel.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>

namespace NovaEngine {
class GameObject;
}  // namespace NovaEngine

namespace NovaEditor {

// 层级面板（§6.5）：树形展示 / F2 重命名 / 拖拽重父子 / 右键菜单 / Del 删除 / Ctrl+D 复制
class HierarchyPanel : public Panel {
public:
    explicit HierarchyPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    enum class PendingOp { Duplicate, Delete };
    struct RowRect { glm::vec2 min; glm::vec2 max; uint64_t id; };

    void drawNode(EditorContext& ctx, NovaEngine::GameObject* go);
    void showContextMenu(EditorContext& ctx, NovaEngine::GameObject* parent, bool onNode);
    void processPending(EditorContext& ctx);   // 树遍历结束后执行（重建场景会让节点指针作废）
    void handleShortcuts(EditorContext& ctx);

    std::vector<RowRect> m_rowRects;                           // 每帧重建：行区域 → 拖放落点判定
    std::vector<std::pair<PendingOp, uint64_t>> m_pendingOps;  // 延迟操作队列
    uint64_t m_renamingId = 0;
    std::string m_renameBuffer;
};

}  // namespace NovaEditor
