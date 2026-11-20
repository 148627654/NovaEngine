// AudioSource.cpp —— 音源组件（V0.3-2）+ V1.0-8 追加（§26.5）
//
// Play/Stop 经 AudioEngine（handle 存组件内 m_handle）；onStart=playOnAwake→Play；
// onUpdate 每帧把位置同步给 AudioEngine（3D 分量位置跟随）；
// 析构 stop（组件销毁/场景重建即停声）。序列化字段走 onSerialize/onDeserialize，
// 反序列化分发经 ComponentRegistry（本文件末尾 NE_REGISTER_COMPONENT 自注册）。
//
// V1.0-8 追加（全部追加式，既有签名零变更）：
//   stream=true → AudioEngine::playStreaming（clipPath 直接给路径，不预解码 AudioClip、
//   不进 AssetDatabase::m_audioClipCache——避免双路径缓存语义混乱，§26.5）
//   fadeInSeconds>0 → 播放成功后 fadeIn；fadeOutSeconds>0 → Stop 先 fadeOut 再延迟 stop
//   （延迟经 onUpdate 计时，到期 stopImmediate；验收只认「无爆音」）
//   析构/重入 Play 走 stopImmediate（立即停——析构 stop 语义不变，无组件可续计时）

#include <NovaEngine/Scene/AudioSource.h>
#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>

#include <nlohmann/json.hpp>   // DevOps 修复：onSerialize 使用 json 运算符，头文件仅 fwd 声明

#include <algorithm>

namespace NovaEngine {

AudioSource::AudioSource(GameObject* owner) : Component(owner) {}

AudioSource::~AudioSource() {
    stopImmediate();   // 析构停声（冻结头契约：「析构/onAwake 失败=stop」；淡出无宿主计时 → 立即停）
}

void AudioSource::stopImmediate() {
    if (m_handle != 0) {
        AudioEngine::stop(m_handle);
        m_handle = 0;
    }
    m_stopPending = false;
    m_stopTimer = 0.0f;
}

void AudioSource::Play() {
    if (clipPath.empty())
        return;
    stopImmediate();   // 重入 Play：先停旧声（同组件只保留一个发声句柄；重播不淡出旧声）

    if (stream) {
        // 流式路径（V1.0-8）：不预解码 AudioClip、不进 m_audioClipCache（§26.5）
        const int busIndex = std::clamp(bus, 0, static_cast<int>(AudioBus::Count) - 1);
        m_handle = AudioEngine::playStreaming(clipPath, static_cast<AudioBus>(busIndex), volume,
                                              loop, spatialBlend, minDistance, maxDistance);
    } else {
        auto clip = AssetDatabase::instance().getAudioClip(clipPath);
        if (!clip) {
            Logger::warn("AudioSource: 音频资产加载失败，无法播放: {}", clipPath);
            return;
        }
        m_handle = AudioEngine::play(clip, transform()->worldPosition(), volume, loop,
                                     spatialBlend, minDistance, maxDistance);
    }
    if (m_handle == 0) {
        Logger::warn("AudioSource: 播放失败（音频引擎不可用或资产无效）: {}", clipPath);
        return;
    }
    if (fadeInSeconds > 0.0f)
        AudioEngine::fadeIn(m_handle, fadeInSeconds);
}

void AudioSource::Stop() {
    if (m_handle == 0) {
        m_stopPending = false;   // 防御：无句柄清挂起态
        m_stopTimer = 0.0f;
        return;
    }
    // 淡出：先 fadeOut（从当前音量线性降到 0），到期再停——无爆音（§26.5 验收）
    if (fadeOutSeconds > 0.0f) {
        AudioEngine::fadeOut(m_handle, fadeOutSeconds);
        m_stopPending = true;
        m_stopTimer = fadeOutSeconds;
        return;
    }
    stopImmediate();
}

void AudioSource::onStart() {
    if (playOnAwake)
        Play();
}

void AudioSource::onUpdate(float dt) {
    // fadeOut 延迟 stop 计时：到期立即停（句柄可能已被惰性清扫回收 → stop no-op）
    if (m_stopPending) {
        m_stopTimer -= dt;
        if (m_stopTimer <= 0.0f)
            stopImmediate();
    }
    // 位置每帧跟随（§14.2）：AudioEngine 内部对 2D 分量 no-op；
    // 句柄可能已被惰性清扫回收（非循环播完）→ setPosition no-op，无副作用
    if (m_handle != 0)
        AudioEngine::setPosition(m_handle, transform()->worldPosition());
}

void AudioSource::onSerialize(nlohmann::json& out) const {
    out["clipPath"] = clipPath;
    out["volume"] = volume;
    out["loop"] = loop;
    out["spatialBlend"] = spatialBlend;
    out["minDistance"] = minDistance;
    out["maxDistance"] = maxDistance;
    out["playOnAwake"] = playOnAwake;
    // V1.0-8 追加（§26.5 键名；旧文件缺省零值兜底）
    out["bus"] = bus;
    out["stream"] = stream;
    out["fadeInSeconds"] = fadeInSeconds;
    out["fadeOutSeconds"] = fadeOutSeconds;
}

void AudioSource::onDeserialize(const nlohmann::json& in) {
    clipPath = in.value("clipPath", std::string());
    volume = in.value("volume", 1.0f);
    loop = in.value("loop", false);
    spatialBlend = in.value("spatialBlend", 1.0f);
    minDistance = in.value("minDistance", 1.0f);
    maxDistance = in.value("maxDistance", 50.0f);
    playOnAwake = in.value("playOnAwake", false);
    bus = in.value("bus", 0);
    stream = in.value("stream", false);
    fadeInSeconds = in.value("fadeInSeconds", 0.0f);
    fadeOutSeconds = in.value("fadeOutSeconds", 0.0f);
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::AudioSource)
