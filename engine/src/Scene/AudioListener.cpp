// AudioListener.cpp —— 音频监听器组件（V0.3-2）
//
// 无字段（§14.2）：约定挂主相机 GO；每帧由编辑器取第一个 active 的监听器
// 同步到 AudioEngine::setListener（见 EditorApplication::updateAudioListener）。
// 反序列化分发经 ComponentRegistry（末尾 NE_REGISTER_COMPONENT 自注册）。

#include <NovaEngine/Scene/AudioListener.h>
#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/GameObject.h>

namespace NovaEngine {

AudioListener::AudioListener(GameObject* owner) : Component(owner) {}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::AudioListener)
