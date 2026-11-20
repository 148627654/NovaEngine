#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <glm/vec3.hpp>

namespace NovaEngine {

class AudioClip;

using AudioHandle = uint64_t;

// 音频总线（V1.0-8，§26.5）：4 总线固定；Master 与既有 setMasterVolume 同源同一变量
enum class AudioBus : uint8_t { Master = 0, Music, Sfx, Ui, Count };

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

    // ===== V1.0-8 追加（§26.5；既有签名零变更）=====
    // 总线音量：Master 与 setMasterVolume 同源；调节即时应用于活动句柄
    static void setBusVolume(AudioBus bus, float v);
    static float busVolume(AudioBus bus);
    // 流式播放：压缩字节经 AssetStream 读入（pak 兼容）后由 ma_decoder 流式解码，
    // 不整载 PCM；relPath 项目内相对路径。失败返回 0
    static AudioHandle playStreaming(const std::filesystem::path& relPath, AudioBus bus,
                                     float volume, bool loop, float spatialBlend,
                                     float minDistance, float maxDistance);
    static void setBus(AudioHandle h, AudioBus bus);   // 播放中改总线
    static void fadeIn(AudioHandle h, float seconds);  // miniaudio 内置 fade 能力消费
    static void fadeOut(AudioHandle h, float seconds); // seconds<=0 = no-op（无淡变）
    // 音频设置持久化（assets/audio/audio-settings.novaaudio，version 1）。
    // 缺失/损坏 → 内置默认 + warn 不崩溃（缺失时自动生成默认文件，与 .novameta 同纪律）
    static bool loadSettings(const std::filesystem::path& relPath);
    static bool saveSettings(const std::filesystem::path& relPath);
};

}  // namespace NovaEngine
