#include "UndoRedoManager.h"

#include <utility>

namespace NovaEditor {

// ===== V0.3-9 文件恢复通道（波 4）=====
// FileRestore 内容约定：压栈方附带「该操作改写的文件 + 改写前内容」（undo 负载——
// undo 返回被撤销条目的 files，方向正确）。redo 返回被重做条目的同一清单（同样为
// 改写前内容）：消费方按 sceneJson 重算改写后内容写回（Apply = 场景→文件重序列化可
// 重算；Revert 不改文件、无需文件清单）——冻结头单内容结构下 redo 文件方向的已知取舍。
// 文件栈与快照栈逐位对齐（files[i] 与 snapshot[i] 同进退），旧 pushSnapshot/undo/redo
// 语义零变化（pushSnapshot 等价 pushSnapshotWithFiles(snapshot, {})）。

void UndoRedoManager::pushSnapshot(std::string snapshot) {
    pushSnapshotWithFiles(std::move(snapshot), {});
}

void UndoRedoManager::pushSnapshotWithFiles(std::string snapshot, std::vector<FileRestore> files) {
    m_redoStack.clear();  // 新编辑分支作废旧 redo 链（快照/文件两侧同步清，保持对齐）
    m_redoFiles.clear();
    m_undoStack.push_back(std::move(snapshot));
    m_undoFiles.push_back(std::move(files));
    while (m_undoStack.size() > kMaxDepth) {  // 超限丢最旧（环形栈；文件栈同步裁剪）
        m_undoStack.pop_front();
        m_undoFiles.pop_front();
    }
}

std::string UndoRedoManager::undo() {
    if (m_undoStack.size() < 2) return {};  // 只剩基线：无可撤销
    m_redoStack.push_back(std::move(m_undoStack.back()));
    m_undoStack.pop_back();
    // 文件栈同步移动（返回值与旧实现逐字相同；仅为维持与 undoFull 混用时的逐位对齐）
    m_redoFiles.push_back(std::move(m_undoFiles.back()));
    m_undoFiles.pop_back();
    return m_undoStack.back();  // 新栈顶 = 应恢复的态
}

std::string UndoRedoManager::redo() {
    if (m_redoStack.empty()) return {};
    m_undoStack.push_back(std::move(m_redoStack.back()));
    m_redoStack.pop_back();
    // 文件栈同步移动（同上——旧 API 语义零变化）
    m_undoFiles.push_back(std::move(m_redoFiles.back()));
    m_redoFiles.pop_back();
    return m_undoStack.back();
}

UndoResult UndoRedoManager::undoFull() {
    if (m_undoStack.size() < 2) return {};  // 只剩基线：无可撤销
    m_redoStack.push_back(std::move(m_undoStack.back()));
    m_undoStack.pop_back();
    m_redoFiles.push_back(std::move(m_undoFiles.back()));
    m_undoFiles.pop_back();
    // 场景 = 新栈顶（应恢复的态）；files = 被撤销条目携带的清单（改写前内容）
    return {m_undoStack.back(), m_redoFiles.back()};
}

UndoResult UndoRedoManager::redoFull() {
    if (m_redoStack.empty()) return {};
    m_undoStack.push_back(std::move(m_redoStack.back()));
    m_redoStack.pop_back();
    m_undoFiles.push_back(std::move(m_redoFiles.back()));
    m_redoFiles.pop_back();
    // 场景 = 新栈顶（应恢复的态）；files = 被重做条目携带的清单（改写前内容，
    // 消费方按 sceneJson 重算改写后内容写回——见顶部约定注释）
    return {m_undoStack.back(), m_undoFiles.back()};
}

void UndoRedoManager::clear(const std::string& baseline) {
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoFiles.clear();  // 文件栈随场景栈一并清空（保持逐位对齐）
    m_redoFiles.clear();
    if (!baseline.empty()) {  // 空基线（序列化失败）不压
        m_undoStack.push_back(baseline);
        m_undoFiles.push_back({});  // 评审 P2-2：基线条目同步入文件栈——「逐位对齐」不变量
                                    // 对 baseline 也成立（防御级；当前仅用 back() 无功能影响）
    }
}

bool UndoRedoManager::canUndo() const { return m_undoStack.size() >= 2; }

bool UndoRedoManager::canRedo() const { return !m_redoStack.empty(); }

size_t UndoRedoManager::undoDepth() const { return m_undoStack.size(); }

size_t UndoRedoManager::redoDepth() const { return m_redoStack.size(); }

}  // namespace NovaEditor
