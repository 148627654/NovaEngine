#pragma once
#include "../Panel.h"

#include <NovaEngine/Core/Logger.h>

#include <deque>
#include <string>

namespace NovaEditor {

// 控制台面板（§6.5）：Logger sink 实现 + deque 环形缓冲（上限 500 条）+ 分级分色 + Clear
class ConsolePanel : public Panel {
public:
    explicit ConsolePanel();   // 构造时 Logger::addSink
    ~ConsolePanel() override;  // 析构时 Logger::clearSinks

    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    struct Entry {
        NovaEngine::LogLevel level;
        std::string message;
    };

    std::deque<Entry> m_entries;
    bool m_showDebug = true, m_showInfo = true, m_showWarn = true, m_showError = true;
    bool m_autoScroll = true;
    int m_newEntries = 0;   // sink 写入计数，渲染时清空（驱动自动滚动）
};

}  // namespace NovaEditor
