#pragma once
#include <cstddef>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace NovaEditor {

// ===== V0.3-9 Prefab 文件恢复通道（波 4 冻结；新方法不破坏旧签名）=====
// Apply/Revert 会改写 .novaprefab 资产文件——undo 不仅恢复场景 JSON，还要恢复文件内容。
struct FileRestore {
    std::filesystem::path path;   // 相对项目根
    std::string content;
};

struct UndoResult {
    std::string sceneJson;                // 空串 = 无可恢复
    std::vector<FileRestore> files;       // 消费方：先写回 files 再 deserialize sceneJson
};

// 快照式 Undo/Redo（V0.2-1）：JSON 字符串环形栈（上限 64）。
// 语义：undo 栈顶（back）恒为当前态；pushSnapshot 在「编辑结束瞬间」压入变更后状态；
// undo/redo 返回应恢复的 JSON 快照（空串 = 无可恢复），恢复 = deserialize 回同一 Scene。
class UndoRedoManager {
public:
    static constexpr size_t kMaxDepth = 64;

    void pushSnapshot(std::string snapshot);  // 操作完成后压「变更后」态；清 redo 栈；超限丢最旧
    std::string undo();                       // 栈顶移入 redo，返回新栈顶；深度<2 返回空串
    std::string redo();                       // redo 栈顶移回 undo，返回新栈顶；空栈返回空串
    void clear(const std::string& baseline);  // New/Open 后清空两栈并压基线
    bool canUndo() const;                     // 深度 >= 2 才可撤销（栈顶即当前态）
    bool canRedo() const;
    size_t undoDepth() const;
    size_t redoDepth() const;

    // ===== V0.3-9 文件恢复通道（波 4 冻结）=====
    void pushSnapshotWithFiles(std::string snapshot, std::vector<FileRestore> files);
    UndoResult undoFull();                    // 旧 undo() 保留（纯场景栈语义不变）
    UndoResult redoFull();

private:
    std::deque<std::string> m_undoStack;            // back = 当前态
    std::deque<std::string> m_redoStack;            // back = 下一次 redo 要恢复的态
    std::deque<std::vector<FileRestore>> m_undoFiles;   // 与 undo 栈逐位对齐（随 push 清 redo 侧）
    std::deque<std::vector<FileRestore>> m_redoFiles;
};

}  // namespace NovaEditor
