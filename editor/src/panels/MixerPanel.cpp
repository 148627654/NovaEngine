// MixerPanel.cpp —— 混音器面板（V1.0-8，§26.5）
//
// Master/Music/Sfx/Ui 四滑条：改动即时 setBusVolume（AudioEngine 遍历活动句柄重算音量，
// 实时生效）；Save 写 assets/audio/audio-settings.novaaudio v1，Reload 读回并刷新滑条。
// Master 与 AudioEngine::setMasterVolume 同源同一变量（§26.5）。

#include "MixerPanel.h"

#include <imgui.h>

#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Core/Logger.h>

namespace NovaEditor {

namespace {
// 音频设置资产路径（与 §26.5 契约一致；loadSettings 接线归合波日，本面板 Save/Reload 直用）
const char* kSettingsRel = "assets/audio/audio-settings.novaaudio";
const char* kBusNames[4] = {"Master", "Music", "Sfx", "Ui"};
}  // namespace

MixerPanel::MixerPanel() : Panel("Mixer") {}

void MixerPanel::refreshFromEngine() {
    using namespace NovaEngine;
    for (int i = 0; i < 4; ++i)
        m_busValues[i] = AudioEngine::busVolume(static_cast<AudioBus>(i));
}

void MixerPanel::onGui(EditorContext& /*ctx*/, EditorCamera&) {
    using namespace NovaEngine;

    if (m_needsRefresh) {
        refreshFromEngine();
        m_needsRefresh = false;
    }

    ImGui::Begin(m_name.c_str(), &open);

    for (int i = 0; i < 4; ++i) {
        if (ImGui::SliderFloat(kBusNames[i], &m_busValues[i], 0.0f, 1.0f, "%.3f"))
            AudioEngine::setBusVolume(static_cast<AudioBus>(i), m_busValues[i]);
    }

    ImGui::Separator();
    if (ImGui::Button("Save")) {
        if (AudioEngine::saveSettings(kSettingsRel))
            Logger::info("混音器：设置已保存（{}）", kSettingsRel);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        AudioEngine::loadSettings(kSettingsRel);
        m_needsRefresh = true;   // 读回后刷新滑条（文件缺失时展示内置默认）
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Settings: %s", kSettingsRel);

    if (!AudioEngine::available())
        ImGui::TextDisabled("音频引擎不可用（无音频设备）——滑条仅记录设置，播放无输出");

    ImGui::End();
}

}  // namespace NovaEditor
