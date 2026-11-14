#pragma once
#include "../Panel.h"

namespace NovaEditor {

// 统计面板（V0.2-11）：FPS / 帧时间直方图 / 分段耗时 / 渲染计数器。
// 纯消费者：数据来自 Core/Stats（帧时序）与 Renderer::last*（计数器）。F8 开关，默认关。
class StatsPanel : public Panel {
public:
    explicit StatsPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;
};

}  // namespace NovaEditor
