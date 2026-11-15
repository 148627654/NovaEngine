// AudioSource.cpp —— 音源组件（V0.3-2）
//
// Play/Stop 经 AudioEngine（handle 存组件内 m_handle）；onStart=playOnAwake→Play；
// onUpdate 每帧把位置同步给 AudioEngine（3D 分量位置跟随 Transform）；
// 析构 stop（组件销毁/场景重建即停声）。序列化字段走 onSerialize/onDeserialize，
// 反序列化分发经 ComponentRegistry（本文件末尾 NE_REGISTER_COMPONENT 自注册）。

#include <NovaEngine/Scene/AudioSource.h>
#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Transform.h>

#include <nlohmann/json.hpp>   // DevOps 修复：onSerialize 使用 json 运算符，头文件仅 fwd 声明

namespace NovaEngine {

AudioSource::AudioSource(GameObject* owner) : Component(owner) {}

AudioSource::~AudioSource() {
    Stop();   // 析构停声（冻结头契约：「析构/onAwake 失败=stop」）
}

void AudioSource::Play() {
    if (clipPath.empty())
        return;
    Stop();   // 重入 Play：先停旧声（同组件只保留一个发声句柄）

    auto clip = AssetDatabase::instance().getAudioClip(clipPath);
    if (!clip) {
        Logger::warn("AudioSource: 音频资产加载失败，无法播放: {}", clipPath);
        return;
    }
    m_handle = AudioEngine::play(clip, transform()->worldPosition(), volume, loop, spatialBlend,
                                 minDistance, maxDistance);
    if (m_handle == 0)
        Logger::warn("AudioSource: 播放失败（音频引擎不可用）: {}", clipPath);
}

void AudioSource::Stop() {
    if (m_handle != 0) {
        AudioEngine::stop(m_handle);
        m_handle = 0;
    }
}

void AudioSource::onStart() {
    if (playOnAwake)
        Play();
}

void AudioSource::onUpdate(float /*dt*/) {
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
}

void AudioSource::onDeserialize(const nlohmann::json& in) {
    clipPath = in.value("clipPath", std::string());
    volume = in.value("volume", 1.0f);
    loop = in.value("loop", false);
    spatialBlend = in.value("spatialBlend", 1.0f);
    minDistance = in.value("minDistance", 1.0f);
    maxDistance = in.value("maxDistance", 50.0f);
    playOnAwake = in.value("playOnAwake", false);
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::AudioSource)
