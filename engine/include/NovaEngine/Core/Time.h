#pragma once
#include <cstdint>

namespace NovaEngine {

class Time {
public:
    static void beginFrame();       // 计算 deltaTime，上限钳制 0.1s
    static float deltaTime();       // 秒
    static float totalTime();       // 启动以来秒数
    static uint64_t frameCount();
};

}  // namespace NovaEngine
