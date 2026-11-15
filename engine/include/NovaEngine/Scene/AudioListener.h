#pragma once
#include "Component.h"

namespace NovaEngine {

// 音频监听器（V0.3-2）：无字段；挂主相机 GO；每帧取第一个 active 的
class AudioListener : public Component {
public:
    explicit AudioListener(GameObject* owner);
};

}  // namespace NovaEngine
