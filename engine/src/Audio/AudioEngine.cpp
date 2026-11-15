// AudioEngine.cpp —— 引擎级音频单例（V0.3-2，miniaudio 后端）
//
// MINIAUDIO_IMPLEMENTATION 为全项目唯一定义点（§12.2，与 stb_image 同纪律）；
// 公共头不泄漏任何 miniaudio 类型。设备线程由 miniaudio 内部管理，全部 API 调用在主线程。
//
// 坐标约定说明（防将来误改）：miniaudio 文档默认右手系「+X 右 / +Y 上 / -Z 前」，
// 但实际声像（panning）按 声音相对监听器的世界坐标 与 扬声器方向（左=-X 右=+X）点积计算，
// 与 NovaEngine（+X 右 / +Y 上 / +Z 前）逐轴一致；监听器方向仅用于锥形衰减（默认 360° 关闭）。
// 因此 NovaEngine 坐标直接透传，无需翻转。
//
// 生命周期（§14.2）：编辑器启动即 init（无设备 → available()=false 优雅降级）；
// 进入 Play 前 stopAll（防爆音）、退出 Play 时 stopAll、退出程序 shutdown。

#define MINIAUDIO_IMPLEMENTATION
// Windows 后端裁剪（§12.2 可选）：仅保留 WASAPI（DirectSound/WinMM 禁用；
// 无可用设备时 init 失败 → 优雅降级路径，见 init()）
#define MA_NO_DSOUND
#define MA_NO_WINMM
#include <miniaudio.h>

#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Audio/AudioClip.h>
#include <NovaEngine/Core/Logger.h>

#include <algorithm>
#include <unordered_map>

namespace NovaEngine {

namespace {

ma_engine s_engine{};       // 零初始化；init() 成功后才可用
bool s_initialized = false;

// 每个播放句柄一个条目。ma_sound/ma_audio_buffer 为 C 结构（无析构），
// 必须手动 init/uninit；条目位于 unordered_map 节点内（节点地址稳定，就地初始化安全）。
struct SoundEntry {
    std::shared_ptr<AudioClip> clip = nullptr;   // 保活 PCM 数据（ma_audio_buffer 引用不拷贝）
    ma_audio_buffer buffer{};
    ma_sound flat{};          // spatialBlend < 1 时有效：全 2D 分量（NO_SPATIALIZATION）
    ma_sound spatial{};       // spatialBlend > 0 时有效：全 3D 分量（线性衰减）
    bool hasBuffer = false, hasFlat = false, hasSpatial = false;
    float volume = 1.0f;      // 基音量（setVolume 覆写；两个分量按 blend 拆分）
    float blend = 1.0f;       // 已钳制 [0,1] 的 spatialBlend
    bool loop = false;
};

std::unordered_map<uint64_t, SoundEntry> s_sounds;
uint64_t s_nextHandle = 1;    // 句柄单调分配；0 = 无效句柄（AudioEngine.h 约定）

// 反初始化一个条目（erase 前必须调用；只反初始化成功初始化的分量）
void releaseEntry(SoundEntry& e) {
    if (e.hasFlat) ma_sound_uninit(&e.flat);
    if (e.hasSpatial) ma_sound_uninit(&e.spatial);
    if (e.hasBuffer) ma_audio_buffer_uninit(&e.buffer);
    e.hasFlat = e.hasSpatial = e.hasBuffer = false;
}

}  // namespace

bool AudioEngine::init() {
    if (s_initialized)
        return true;

    // 配置传 nullptr = 全默认（设备格式/采样率跟随硬件；miniaudio 0.11.26 文档允许）
    const ma_result result = ma_engine_init(nullptr, &s_engine);
    if (result != MA_SUCCESS) {
        // 无设备 / 后端初始化失败：优雅降级（available()=false，其余功能不受影响）
        Logger::warn("音频引擎初始化失败：{}（无可用音频设备），音频功能已禁用",
                     ma_result_description(result));
        return false;
    }
    s_initialized = true;
    Logger::info("音频引擎初始化成功（miniaudio {}）", MA_VERSION_STRING);
    return true;
}

void AudioEngine::shutdown() {
    if (!s_initialized)
        return;
    stopAll();
    ma_engine_uninit(&s_engine);
    s_initialized = false;
    Logger::info("音频引擎已关闭");
}

bool AudioEngine::available() {
    return s_initialized;
}

void AudioEngine::setListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up) {
    if (!s_initialized)
        return;
    // 坐标透传（见文件头注释：panning 基于世界坐标相对位置，与 NovaEngine 逐轴一致）
    ma_engine_listener_set_position(&s_engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&s_engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&s_engine, 0, up.x, up.y, up.z);
}

AudioHandle AudioEngine::play(const std::shared_ptr<AudioClip>& clip, const glm::vec3& pos,
                              float volume, bool loop, float spatialBlend,
                              float minDistance, float maxDistance) {
    if (!s_initialized || !clip || clip->channels() == 0 || clip->pcmF32().empty())
        return 0;

    // 惰性清扫：回收已自然播完的非循环句柄（循环句柄永不到头；防 map 无限增长）
    for (auto it = s_sounds.begin(); it != s_sounds.end();) {
        SoundEntry& e = it->second;
        const bool ended = !e.loop && (!e.hasFlat || ma_sound_at_end(&e.flat)) &&
                           (!e.hasSpatial || ma_sound_at_end(&e.spatial));
        if (ended) {
            releaseEntry(e);
            it = s_sounds.erase(it);
        } else {
            ++it;
        }
    }

    // 句柄分配（0 保留为无效；2^64 次播放后才可能回绕，忽略）
    AudioHandle h = s_nextHandle++;
    if (h == 0)
        h = s_nextHandle++;

    // 就地初始化（避免移动 ma_sound——C 结构无移动语义，直接构建在 map 节点内）
    SoundEntry& entry = s_sounds[h];
    entry.clip = clip;
    entry.loop = loop;
    entry.blend = std::clamp(spatialBlend, 0.0f, 1.0f);
    entry.volume = std::max(0.0f, volume);

    // 数据源：ma_audio_buffer 引用 AudioClip 已解码的 PCM（不拷贝；entry.clip 保活）
    const ma_uint64 frameCount = clip->pcmF32().size() / clip->channels();
    ma_audio_buffer_config bufCfg = ma_audio_buffer_config_init(
        ma_format_f32, clip->channels(), frameCount, clip->pcmF32().data(), nullptr);
    if (ma_audio_buffer_init(&bufCfg, &entry.buffer) != MA_SUCCESS) {
        Logger::error("AudioEngine: 音频缓冲初始化失败 {}", clip->path().string());
        s_sounds.erase(h);
        return 0;
    }
    entry.hasBuffer = true;

    // 3D 分量（spatialBlend > 0）：默认空间化 + 线性衰减（minDistance 处 1、maxDistance 处 0，
    // Unity 语义）；默认锥形 360° = 全向（miniaudio 默认，无需改）
    if (entry.blend > 0.0f) {
        if (ma_sound_init_from_data_source(&s_engine, &entry.buffer, 0, nullptr,
                                           &entry.spatial) == MA_SUCCESS) {
            entry.hasSpatial = true;
            ma_sound_set_looping(&entry.spatial, loop ? MA_TRUE : MA_FALSE);
            ma_sound_set_volume(&entry.spatial, entry.volume * entry.blend);
            ma_sound_set_attenuation_model(&entry.spatial, ma_attenuation_model_linear);
            ma_sound_set_min_distance(&entry.spatial, std::max(0.0f, minDistance));
            ma_sound_set_max_distance(&entry.spatial, std::max(minDistance, maxDistance));
            ma_sound_set_position(&entry.spatial, pos.x, pos.y, pos.z);
        }
    }
    // 2D 分量（spatialBlend < 1）：NO_SPATIALIZATION 全音量，无视位置/距离
    if (entry.blend < 1.0f) {
        if (ma_sound_init_from_data_source(&s_engine, &entry.buffer, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                           nullptr, &entry.flat) == MA_SUCCESS) {
            entry.hasFlat = true;
            ma_sound_set_looping(&entry.flat, loop ? MA_TRUE : MA_FALSE);
            ma_sound_set_volume(&entry.flat, entry.volume * (1.0f - entry.blend));
        }
    }

    if (!entry.hasFlat && !entry.hasSpatial) {
        Logger::error("AudioEngine: 声音初始化失败 {}", clip->path().string());
        releaseEntry(entry);
        s_sounds.erase(h);
        return 0;
    }
    return h;
}

void AudioEngine::setPosition(AudioHandle h, const glm::vec3& pos) {
    if (!s_initialized || h == 0)
        return;
    auto it = s_sounds.find(h);
    if (it == s_sounds.end())
        return;   // 句柄已被惰性清扫/stop 回收：no-op
    if (it->second.hasSpatial)
        ma_sound_set_position(&it->second.spatial, pos.x, pos.y, pos.z);
    // 2D 分量无位置概念
}

void AudioEngine::setVolume(AudioHandle h, float v) {
    if (!s_initialized || h == 0)
        return;
    auto it = s_sounds.find(h);
    if (it == s_sounds.end())
        return;
    SoundEntry& e = it->second;
    e.volume = std::max(0.0f, v);
    if (e.hasSpatial)
        ma_sound_set_volume(&e.spatial, e.volume * e.blend);
    if (e.hasFlat)
        ma_sound_set_volume(&e.flat, e.volume * (1.0f - e.blend));
}

void AudioEngine::stop(AudioHandle h) {
    if (h == 0)
        return;
    auto it = s_sounds.find(h);
    if (it == s_sounds.end())
        return;
    releaseEntry(it->second);
    s_sounds.erase(it);
}

void AudioEngine::stopAll() {
    // 退出 Play / LoadScene 调用（§14.2 生命周期，防爆音）
    for (auto& kv : s_sounds)
        releaseEntry(kv.second);
    s_sounds.clear();
}

void AudioEngine::setMasterVolume(float v) {
    if (s_initialized)
        ma_engine_set_volume(&s_engine, std::clamp(v, 0.0f, 1.0f));
}

}  // namespace NovaEngine
