#pragma once
#include <cstdint>
#include <memory>
#include <glm/vec3.hpp>

namespace NovaEngine {

class AudioClip;

using AudioHandle = uint64_t;

// 引擎级音频单例（V0.3-2，miniaudio 后端）：设备线程由 miniaudio 内部管理；
// 全部 API 调用在主线程。公共头无 miniaudio 类型。
class AudioEngine {
public:
    static bool init();                           // 编辑器/Player 启动调用；无设备 → false + 警告
    static void shutdown();
    static bool available();                      // 优雅降级查询
    static void setListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up);
    static AudioHandle play(const std::shared_ptr<AudioClip>& clip, const glm::vec3& pos,
                            float volume, bool loop, float spatialBlend,
                            float minDistance, float maxDistance);   // 失败返回 0
    static void setPosition(AudioHandle h, const glm::vec3& pos);
    static void setVolume(AudioHandle h, float v);
    static void stop(AudioHandle h);
    static void stopAll();                        // 退出 Play / LoadScene 调用（防爆音）
    static void setMasterVolume(float v);
};

}  // namespace NovaEngine
