#include "EditorContext.h"

namespace NovaEditor {

NovaEngine::GameObject* EditorContext::activeObject() {
    if (selectedIds.size() != 1) return nullptr;
    const uint64_t id = *selectedIds.begin();
    if (!scene) return nullptr;
    NovaEngine::GameObject* go = scene->findGameObject(id);
    if (!go) selectedIds.erase(id);  // 对象已销毁：清理失效 ID
    return go;
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

}  // namespace NovaEditor
