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
    // ===== V1.0-8 追加（§26.5；序列化缺省零值兜底，场景 version 4 不变）=====
    int bus = 0;                    // AudioBus 枚举值（0=Master）
    bool stream = false;            // true = 流式播放（长 BGM，不预解码 AudioClip）；false = 既有整载 PCM
    float fadeInSeconds = 0.0f;     // 0 = 无淡变
    float fadeOutSeconds = 0.0f;    // 0 = 无淡变（Stop 直接停）

    void Play();
    void Stop();                    // fadeOutSeconds>0 时先淡出、到期再停（无爆音）

    void onStart() override;                      // playOnAwake → Play
    void onUpdate(float dt) override;             // 位置跟随 + fadeOut 延迟 stop 计时
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;

private:
    AudioHandle m_handle = 0;   // AudioEngine 播放句柄（0=无效）；句柄存组件内（V0.3-2 任务书）
    bool m_stopPending = false; // fadeOut 延迟 stop 挂起（onUpdate 到期执行）
    float m_stopTimer = 0.0f;

    void stopImmediate();       // 直接停声（析构/重入 Play/无淡变路径；析构语义不变）
};

}  // namespace NovaEngine
