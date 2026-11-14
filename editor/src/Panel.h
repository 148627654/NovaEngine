#pragma once
#include <string>

namespace NovaEditor {

class EditorContext;
class EditorCamera;

// 编辑器面板抽象基类（§2.9）
class Panel {
public:
    explicit Panel(const std::string& name) : m_name(name) {}
    virtual ~Panel() = default;

    // 每帧在 EditorApplication::update 中被调用
    virtual void onGui(EditorContext& ctx, EditorCamera& camera) = 0;

    const std::string& name() const { return m_name; }
    bool open = true;

protected:
    std::string m_name;
};

}  // namespace NovaEditor
