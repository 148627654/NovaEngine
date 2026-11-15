#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Audio/AudioEngine.h>   // AudioHandle（公共头，无 miniaudio 依赖）
#include "Component.h"

namespace NovaEngine {

// 音源（V0.3-2）：位置每帧跟随 Transform；析构/onAwake 失败=stop
class AudioSource : public Component {
public:
    explicit AudioSource(GameObject* owner);
    ~AudioSource() override;   // 析构 stop：组件销毁/场景重建即停声，防声音残留

    std::string clipPath;                         // 资产相对路径（空 = 无效）
    float volume = 1.0f;
    bool loop = false;
    float spatialBlend = 1.0f;                    // 0=2D 全音量，1=3D 距离衰减
    float minDistance = 1.0f, maxDistance = 50.0f;
    bool playOnAwake = false;

    void Play();
    void Stop();

    void onStart() override;                      // playOnAwake → Play
    void onUpdate(float dt) override;             // 位置跟随
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

private:
    AudioHandle m_handle = 0;   // AudioEngine 播放句柄（0=无效）；句柄存组件内（V0.3-2 任务书）
};

}  // namespace NovaEngine
