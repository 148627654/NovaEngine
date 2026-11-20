#pragma once
#include "../Panel.h"

namespace NovaEditor {

// 混音器面板（V1.0-8，§26.5）：Master/Music/Sfx/Ui 四滑条 + Save/Reload。
// 滑条调节实时生效（AudioEngine::setBusVolume 即时应用于活动句柄）；
// Save/Reload 读写 assets/audio/audio-settings.novaaudio。
// 不 dock（合波日接线）；编辑器/Player main 的 loadSettings 接线归合波日（主会话）。
class MixerPanel : public Panel {
public:
    explicit MixerPanel();
    void onGui(EditorContext& ctx, EditorCamera& camera) override;

private:
    void refreshFromEngine();

    float m_busValues[4] = {1.0f, 1.0f, 1.0f, 1.0f};   // 展示缓存（首帧/Reload 后刷新）
    bool m_needsRefresh = true;
};

}  // namespace NovaEditor
