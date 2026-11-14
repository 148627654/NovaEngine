#include "UndoRedoManager.h"

#include <utility>

namespace NovaEditor {

void UndoRedoManager::pushSnapshot(std::string snapshot) {
    m_redoStack.clear();  // 新编辑分支作废旧 redo 链
    m_undoStack.push_back(std::move(snapshot));
    while (m_undoStack.size() > kMaxDepth)  // 超限丢最旧（环形栈）
        m_undoStack.pop_front();
}

std::string UndoRedoManager::undo() {
    if (m_undoStack.size() < 2) return {};  // 只剩基线：无可撤销
    m_redoStack.push_back(std::move(m_undoStack.back()));
    m_undoStack.pop_back();
    return m_undoStack.back();  // 新栈顶 = 应恢复的态
}

std::string UndoRedoManager::redo() {
    if (m_redoStack.empty()) return {};
    m_undoStack.push_back(std::move(m_redoStack.back()));
    m_redoStack.pop_back();
    return m_undoStack.back();
}

void UndoRedoManager::clear(const std::string& baseline) {
    m_undoStack.clear();
    m_redoStack.clear();
    if (!baseline.empty()) m_undoStack.push_back(baseline);  // 空基线（序列化失败）不压
}

bool UndoRedoManager::canUndo() const { return m_undoStack.size() >= 2; }

bool UndoRedoManager::canRedo() const { return !m_redoStack.empty(); }

size_t UndoRedoManager::undoDepth() const { return m_undoStack.size(); }

size_t UndoRedoManager::redoDepth() const { return m_redoStack.size(); }

}  // namespace NovaEditor
