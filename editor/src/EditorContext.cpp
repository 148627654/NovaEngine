#include "EditorContext.h"

#include <NovaEngine/Serialization/SceneSerializer.h>

namespace NovaEditor {

NovaEngine::GameObject* EditorContext::activeObject() {
    if (selectedIds.size() != 1) return nullptr;
    const uint64_t id = *selectedIds.begin();
    if (!scene) return nullptr;
    NovaEngine::GameObject* go = scene->findGameObject(id);
    if (!go) selectedIds.erase(id);  // 对象已销毁：清理失效 ID
    return go;
}

std::vector<NovaEngine::GameObject*> EditorContext::selectedObjects() {
    std::vector<NovaEngine::GameObject*> out;
    if (!scene) return out;
    std::vector<uint64_t> stale;
    for (uint64_t id : selectedIds) {   // 按集合迭代顺序（稳定），保持选中顺序
        NovaEngine::GameObject* go = scene->findGameObject(id);
        if (go) out.push_back(go);
        else stale.push_back(id);       // 对象已销毁：清理失效 ID
    }
    for (uint64_t id : stale) selectedIds.erase(id);
    return out;
}

void EditorContext::setSelected(uint64_t id) {
    selectedIds.clear();
    selectedIds.insert(id);
}

void EditorContext::toggleSelected(uint64_t id) {
    if (!selectedIds.erase(id)) selectedIds.insert(id);
}

void EditorContext::clearSelection() {
    selectedIds.clear();
}

bool EditorContext::isSelected(uint64_t id) const {
    return selectedIds.contains(id);
}

void EditorContext::markDirtyOnly() {
    sceneDirty = true;  // 拖拽/输入中间帧：只置脏，不推快照（防刷栈）
}

void EditorContext::markSceneDirty() {
    sceneDirty = true;
    // Play 态只置脏不入栈（live edit 结构修改照旧禁用；退出 Play 时 sceneDirty 会还原）
    if (state != EditorState::Edit || !scene) return;
    const std::string snap = NovaEngine::SceneSerializer::serialize(*scene, -1);  // 紧凑快照
    if (!snap.empty()) undoRedo.pushSnapshot(snap);
}

}  // namespace NovaEditor
