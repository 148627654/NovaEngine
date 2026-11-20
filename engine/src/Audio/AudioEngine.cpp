// AudioEngine.cpp —— 引擎级音频单例（V0.3-2，miniaudio 后端）+ V1.0-8 扩展（§26.5）
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
//
// V1.0-8 追加（§26.5，全部追加式，既有签名零变更）：
//   AudioBus 4 总线：Master 与 setMasterVolume 同源（同一变量 s_masterVolume，
//   经 ma_engine 引擎级音量生效，不叠加到逐句柄音量上）；Music/Sfx/Ui 为逐句柄乘数，
//   setBusVolume 即时遍历活动句柄重算有效音量（实时生效）。
//   流式播放：AssetStream 读压缩字节（pak 兼容）→ ma_decoder 流式解码回调
//   （miniaudio 文件流式同款数据源机制，不整载 PCM）→ ma_sound 数据源消费；
//   解码器/字节随句柄条目保活，句柄池与整载路径共用（§26.5「流式句柄同池管理」）。
//   淡入淡出：ma_sound_set_fade_in_milliseconds（库能力消费；volumeBeg=-1 = 从当前音量）。
//   设置持久化：assets/audio/audio-settings.novaaudio v1；缺失自动生成默认、损坏回退默认。

#define MINIAUDIO_IMPLEMENTATION
// Windows 后端裁剪（§12.2 可选）：仅保留 WASAPI（DirectSound/WinMM 禁用；
// 无可用设备时 init 失败 → 优雅降级路径，见 init()）
#define MA_NO_DSOUND
#define MA_NO_WINMM
#include <miniaudio.h>

#include <NovaEngine/Audio/AudioEngine.h>
#include <NovaEngine/Audio/AudioClip.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace NovaEngine {

namespace {

ma_engine s_engine{};       // 零初始化；init() 成功后才可用
bool s_initialized = false;

// 总线音量（V1.0-8）：Master 同源 s_masterVolume（引擎级，见文件头）；
// Music/Sfx/Ui 为逐句柄乘数，索引 = bus-1
float s_masterVolume = 1.0f;
std::array<float, 3> s_busVolumes = {1.0f, 1.0f, 1.0f};

// 每个播放句柄一个条目。ma_sound/ma_audio_buffer/ma_decoder 为 C 结构（无析构），
// 必须手动 init/uninit；条目位于 unordered_map 节点内（节点地址稳定，就地初始化安全）。
// 两条数据路径（§26.5）：整载（play：clip + ma_audio_buffer 引用已解码 PCM）
// 与流式（playStreaming：streamBytes 压缩字节保活 + ma_decoder 流式解码，不整载 PCM）。
struct SoundEntry {
    std::shared_ptr<AudioClip> clip = nullptr;   // 整载：保活 PCM 数据（ma_audio_buffer 引用不拷贝）
    ma_audio_buffer buffer{};
    bool hasBuffer = false;
    std::vector<uint8_t> streamBytes;            // 流式：压缩字节保活（ma_decoder 引用不拷贝）
    ma_decoder decoder{};
    bool hasDecoder = false;
    ma_sound flat{};          // spatialBlend < 1 时有效：全 2D 分量（NO_SPATIALIZATION）
    ma_sound spatial{};       // spatialBlend > 0 时有效：全 3D 分量（线性衰减）
    bool hasFlat = false, hasSpatial = false;
    float volume = 1.0f;      // 基音量（setVolume 覆写；两个分量按 blend 拆分）
    float blend = 1.0f;       // 已钳制 [0,1] 的 spatialBlend
    bool loop = false;
    AudioBus bus = AudioBus::Master;   // V1.0-8：播放所在总线
};

std::unordered_map<uint64_t, SoundEntry> s_sounds;
uint64_t s_nextHandle = 1;    // 句柄单调分配；0 = 无效句柄（AudioEngine.h 约定）

// 反初始化一个条目（erase 前必须调用；只反初始化成功初始化的分量；
// 顺序：声音（引用解码器）→ 解码器 → 缓冲，字节随条目析构释放）
void releaseEntry(SoundEntry& e) {
    if (e.hasFlat) ma_sound_uninit(&e.flat);
    if (e.hasSpatial) ma_sound_uninit(&e.spatial);
    if (e.hasDecoder) ma_decoder_uninit(&e.decoder);
    if (e.hasBuffer) ma_audio_buffer_uninit(&e.buffer);
    e.hasFlat = e.hasSpatial = e.hasDecoder = e.hasBuffer = false;
    e.streamBytes.clear();
    e.clip.reset();
}

// 惰性清扫：回收已自然播完的非循环句柄（循环句柄永不到头；防 map 无限增长）
void sweepEndedSounds() {
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
}

// 总线乘数：Master 恒 1（引擎级音量已覆盖，见 setMasterVolume），其余取总线音量
float busFactor(AudioBus bus) {
    if (bus == AudioBus::Master)
        return 1.0f;
    const int i = static_cast<int>(bus) - 1;
    return (i >= 0 && i < static_cast<int>(s_busVolumes.size())) ? s_busVolumes[static_cast<size_t>(i)] : 1.0f;
}

// 重算条目两个分量的有效音量（基音量 × 总线 × blend 拆分）
void applyEntryVolume(SoundEntry& e) {
    const float bv = busFactor(e.bus);
    if (e.hasSpatial)
        ma_sound_set_volume(&e.spatial, e.volume * bv * e.blend);
    if (e.hasFlat)
        ma_sound_set_volume(&e.flat, e.volume * bv * (1.0f - e.blend));
}

// 与 AssetDatabase::resolveAssetAbs 同语义的读取锚点（pak 挂载 → 恒项目根；
// 磁盘项目根存在 → 用之；否则 cwd 存在 → 用之（Player 裸盘模式）；否则保留项目根锚点）
std::filesystem::path resolveAssetAbs(const std::filesystem::path& relativePath) {
    const std::filesystem::path projAbs = AssetDatabase::projectRoot() / relativePath;
    if (AssetStream::pakMounted()) return projAbs;
    std::error_code ec;
    if (std::filesystem::exists(projAbs, ec)) return projAbs;
    std::error_code cwdEc;
    const std::filesystem::path cwdAbs = std::filesystem::current_path(cwdEc) / relativePath;
    if (!cwdEc && std::filesystem::exists(cwdAbs, cwdEc)) return cwdAbs;
    return projAbs;
}

// 扩展名 → 解码格式 hint（与 AudioClip.cpp 同表；流式解码器不走扩展名自动探测，
// 内存源必须显式给 hint——miniaudio 同款约定）
ma_encoding_format encodingFromExtension(const std::filesystem::path& relPath) {
    std::string ext = relPath.extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".wav") return ma_encoding_format_wav;
    if (ext == ".mp3") return ma_encoding_format_mp3;
    if (ext == ".flac") return ma_encoding_format_flac;
    if (ext == ".ogg") return ma_encoding_format_vorbis;
    return ma_encoding_format_unknown;
}

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
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
    ma_engine_set_volume(&s_engine, s_masterVolume);   // init 前 setMasterVolume 存值 → 此处补应用
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

    sweepEndedSounds();

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
        }
    }

    if (!entry.hasFlat && !entry.hasSpatial) {
        Logger::error("AudioEngine: 声音初始化失败 {}", clip->path().string());
        releaseEntry(entry);
        s_sounds.erase(h);
        return 0;
    }
    applyEntryVolume(entry);
    return h;
}

AudioHandle AudioEngine::playStreaming(const std::filesystem::path& relPath, AudioBus bus,
                                       float volume, bool loop, float spatialBlend,
                                       float minDistance, float maxDistance) {
    if (!s_initialized || relPath.empty())
        return 0;

    const ma_encoding_format encoding = encodingFromExtension(relPath);
    if (encoding == ma_encoding_format_unknown) {
        Logger::error("AudioEngine: 流式播放不支持扩展名 '{}'（支持 .wav/.mp3/.flac/.ogg）",
                      relPath.extension().string());
        return 0;
    }

    // 压缩字节经 AssetStream（pak 命中优先、磁盘回退）——不整载 PCM
    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(resolveAssetAbs(relPath), bytes) || bytes.empty()) {
        Logger::error("AudioEngine: 流式播放读取失败 {}", relPath.string());
        return 0;
    }

    sweepEndedSounds();

    AudioHandle h = s_nextHandle++;
    if (h == 0)
        h = s_nextHandle++;

    SoundEntry& entry = s_sounds[h];
    entry.bus = (bus >= AudioBus::Master && bus < AudioBus::Count) ? bus : AudioBus::Master;
    entry.loop = loop;
    entry.blend = std::clamp(spatialBlend, 0.0f, 1.0f);
    entry.volume = std::max(0.0f, volume);
    entry.streamBytes = std::move(bytes);

    // 流式解码器：内存源 + 显式格式 hint（扩展名自动探测对内存源不可用）；
    // ma_sound 经数据源回调按需拉取解码（miniaudio 文件流式同款机制，不整载 PCM）
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    cfg.encodingFormat = encoding;
    if (ma_decoder_init_memory(entry.streamBytes.data(), entry.streamBytes.size(), &cfg,
                               &entry.decoder) != MA_SUCCESS) {
        Logger::error("AudioEngine: 流式解码器初始化失败（文件损坏或格式不符）{}",
                      relPath.string());
        s_sounds.erase(h);
        return 0;
    }
    entry.hasDecoder = true;

    if (entry.blend > 0.0f) {
        if (ma_sound_init_from_data_source(&s_engine, &entry.decoder, 0, nullptr,
                                           &entry.spatial) == MA_SUCCESS) {
            entry.hasSpatial = true;
            ma_sound_set_looping(&entry.spatial, loop ? MA_TRUE : MA_FALSE);
            ma_sound_set_attenuation_model(&entry.spatial, ma_attenuation_model_linear);
            ma_sound_set_min_distance(&entry.spatial, std::max(0.0f, minDistance));
            ma_sound_set_max_distance(&entry.spatial, std::max(minDistance, maxDistance));
            ma_sound_set_position(&entry.spatial, 0.0f, 0.0f, 0.0f);
        }
    }
    if (entry.blend < 1.0f) {
        if (ma_sound_init_from_data_source(&s_engine, &entry.decoder,
                                           MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                           &entry.flat) == MA_SUCCESS) {
            entry.hasFlat = true;
            ma_sound_set_looping(&entry.flat, loop ? MA_TRUE : MA_FALSE);
        }
    }

    if (!entry.hasFlat && !entry.hasSpatial) {
        Logger::error("AudioEngine: 流式声音初始化失败 {}", relPath.string());
        releaseEntry(entry);
        s_sounds.erase(h);
        return 0;
    }
    applyEntryVolume(entry);
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
    applyEntryVolume(e);
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
    // V1.0-8：同源变量（busVolume(Master) 与 setMasterVolume 等价）；
    // 引擎未初始化时也存值（loadSettings 可先于 init 语义上生效）
    s_masterVolume = clamp01(v);
    if (s_initialized)
        ma_engine_set_volume(&s_engine, s_masterVolume);
}

// ==================== V1.0-8 追加（§26.5）====================

void AudioEngine::setBusVolume(AudioBus bus, float v) {
    if (bus == AudioBus::Master) {
        setMasterVolume(v);
        return;
    }
    const int i = static_cast<int>(bus) - 1;
    if (i < 0 || i >= static_cast<int>(s_busVolumes.size()))
        return;   // 越界/Count：no-op
    s_busVolumes[static_cast<size_t>(i)] = clamp01(v);
    if (!s_initialized)
        return;
    // 实时生效：遍历活动句柄重算该总线有效音量
    for (auto& kv : s_sounds)
        if (kv.second.bus == bus)
            applyEntryVolume(kv.second);
}

float AudioEngine::busVolume(AudioBus bus) {
    if (bus == AudioBus::Master)
        return s_masterVolume;
    const int i = static_cast<int>(bus) - 1;
    if (i < 0 || i >= static_cast<int>(s_busVolumes.size()))
        return 1.0f;
    return s_busVolumes[static_cast<size_t>(i)];
}

void AudioEngine::setBus(AudioHandle h, AudioBus bus) {
    if (h == 0 || bus < AudioBus::Master || bus >= AudioBus::Count)
        return;
    auto it = s_sounds.find(h);
    if (it == s_sounds.end())
        return;   // 句柄已被回收：no-op
    it->second.bus = bus;
    if (s_initialized)
        applyEntryVolume(it->second);
}

void AudioEngine::fadeIn(AudioHandle h, float seconds) {
    if (!s_initialized || h == 0 || seconds <= 0.0f)
        return;
    auto it = s_sounds.find(h);
    if (it == s_sounds.end())
        return;
    SoundEntry& e = it->second;
    const ma_uint64 ms = static_cast<ma_uint64>(seconds * 1000.0f);
    const float bv = busFactor(e.bus);
    // 0 → 当前有效音量（库能力消费：ma_sound_set_fade_in_milliseconds）
    if (e.hasSpatial)
        ma_sound_set_fade_in_milliseconds(&e.spatial, 0.0f, e.volume * bv * e.blend, ms);
    if (e.hasFlat)
        ma_sound_set_fade_in_milliseconds(&e.flat, 0.0f, e.volume * bv * (1.0f - e.blend), ms);
}

void AudioEngine::fadeOut(AudioHandle h, float seconds) {
    if (!s_initialized || h == 0 || seconds <= 0.0f)
        return;
    auto it = s_sounds.find(h);
    if (it == s_sounds.end())
        return;
    SoundEntry& e = it->second;
    const ma_uint64 ms = static_cast<ma_uint64>(seconds * 1000.0f);
    // 当前音量 → 0（volumeBeg=-1 = 从当前音量起，miniaudio 约定）
    if (e.hasSpatial)
        ma_sound_set_fade_in_milliseconds(&e.spatial, -1.0f, 0.0f, ms);
    if (e.hasFlat)
        ma_sound_set_fade_in_milliseconds(&e.flat, -1.0f, 0.0f, ms);
}

bool AudioEngine::loadSettings(const std::filesystem::path& relPath) {
    // 内置默认：全 1.0
    float master = 1.0f, music = 1.0f, sfx = 1.0f, ui = 1.0f;
    bool loaded = false;

    std::string text;
    const std::filesystem::path abs = AssetDatabase::projectRoot() / relPath;
    if (AssetStream::readAllText(abs, text)) {
        try {
            const nlohmann::json doc = nlohmann::json::parse(text);
            if (!doc.is_object()) {
                Logger::warn("音频设置损坏（根节点非对象），使用内置默认: {}", relPath.string());
            } else {
                master = clamp01(doc.value("master", 1.0f));
                if (doc.contains("buses") && doc["buses"].is_object()) {
                    const nlohmann::json& b = doc["buses"];
                    music = clamp01(b.value("music", 1.0f));
                    sfx = clamp01(b.value("sfx", 1.0f));
                    ui = clamp01(b.value("ui", 1.0f));
                }
                const int version = doc.value("version", 0);
                if (version != 1)
                    Logger::warn("音频设置版本 {}（期望 1），仍按默认规则读取: {}",
                                 version, relPath.string());
                loaded = true;
            }
        } catch (const std::exception& e) {
            Logger::warn("音频设置损坏（{}），使用内置默认: {}", e.what(), relPath.string());
        }
    } else {
        Logger::warn("音频设置文件缺失，使用内置默认并生成默认文件: {}", relPath.string());
        // 缺失自动生成默认文件（与 .novameta 同纪律，§26.5 接线）
        saveSettings(relPath);
    }

    // 应用（setBusVolume 即时作用于活动句柄；未 init 时仅存值）
    setMasterVolume(master);
    setBusVolume(AudioBus::Music, music);
    setBusVolume(AudioBus::Sfx, sfx);
    setBusVolume(AudioBus::Ui, ui);
    return loaded;
}

bool AudioEngine::saveSettings(const std::filesystem::path& relPath) {
    if (relPath.empty()) {
        Logger::error("音频设置保存失败：路径为空");
        return false;
    }
    nlohmann::json doc;
    doc["version"] = 1;
    doc["master"] = s_masterVolume;
    nlohmann::json buses;
    buses["music"] = busVolume(AudioBus::Music);
    buses["sfx"] = busVolume(AudioBus::Sfx);
    buses["ui"] = busVolume(AudioBus::Ui);
    doc["buses"] = std::move(buses);

    const std::filesystem::path abs = AssetDatabase::projectRoot() / relPath;
    std::error_code ec;
    std::filesystem::create_directories(abs.parent_path(), ec);   // 父目录可能不存在
    std::ofstream out(abs, std::ios::binary | std::ios::trunc);   // path 重载：中文路径安全
    if (!out) {
        Logger::error("音频设置保存失败：无法写入 {}", abs.string());
        return false;
    }
    out << doc.dump(4) << '\n';
    if (!out.good()) {
        Logger::error("音频设置保存失败：写入出错 {}", abs.string());
        return false;
    }
    Logger::info("音频设置已保存: {}", abs.string());
    return true;
}

}  // namespace NovaEngine
