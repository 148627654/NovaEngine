// AudioClip.cpp —— 解码后 PCM 音频资产（V0.3-2）
//
// 流程：AssetStream::readAllBytes（pak 兼容、中文路径安全）→ ma_decoder_init_memory
//（内存解码无文件扩展名可用，必须显式给 encodingFormat hint）→ 全量解码为交错 f32 PCM
//（保留源 channels/sampleRate）。v1 全量解码（流式解码列后续优化）。

// 与 AudioEngine.cpp 保持相同的后端裁剪宏（两个 TU 均包含 miniaudio.h；
// 宏只裁剪实现段后端代码，声明段/结构布局一致）
#define MA_NO_DSOUND
#define MA_NO_WINMM
#include <miniaudio.h>

#include <NovaEngine/Audio/AudioClip.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>

#include <cctype>
#include <string>

namespace NovaEngine {

namespace {

// 扩展名 → 编码格式 hint（小写比较；支持 wav/mp3/flac/ogg，§14.2）
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

}  // namespace

std::shared_ptr<AudioClip> AudioClip::load(const std::filesystem::path& relPath) {
    if (relPath.empty())
        return nullptr;

    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(relPath, bytes) || bytes.empty()) {
        Logger::error("AudioClip: 读取音频文件失败 {}", relPath.string());
        return nullptr;
    }

    const ma_encoding_format encoding = encodingFromExtension(relPath);
    if (encoding == ma_encoding_format_unknown) {
        Logger::error("AudioClip: 不支持的音频扩展名 '{}'（支持 .wav/.mp3/.flac/.ogg）",
                      relPath.extension().string());
        return nullptr;
    }

    // 输出格式固定 f32；channels/sampleRate 置 0 = 跟随源流
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    cfg.encodingFormat = encoding;
    ma_decoder decoder;
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), &cfg, &decoder) != MA_SUCCESS) {
        Logger::error("AudioClip: 解码器初始化失败（文件损坏或格式不符）{}", relPath.string());
        return nullptr;
    }

    // DevOps 修复：make_shared 在 std 命名空间内构造，无法访问私有默认构造 → 改为成员函数内直接 new
    std::shared_ptr<AudioClip> clip(new AudioClip());
    clip->m_path = relPath;
    clip->m_channels = decoder.outputChannels;
    clip->m_sampleRate = decoder.outputSampleRate;

    // 全量解码：已知长度一次读完；未知长度（部分流式编码）分块循环读
    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS &&
        totalFrames > 0 && clip->m_channels > 0) {
        clip->m_pcm.resize(static_cast<size_t>(totalFrames) * clip->m_channels);
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&decoder, clip->m_pcm.data(), totalFrames, &read);
        clip->m_pcm.resize(static_cast<size_t>(read) * clip->m_channels);
    } else if (clip->m_channels > 0) {
        const ma_uint64 kChunkFrames = 4096;
        std::vector<float> chunk(static_cast<size_t>(kChunkFrames) * clip->m_channels);
        ma_uint64 read = 0;
        while (ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &read) ==
                   MA_SUCCESS &&
               read > 0) {
            clip->m_pcm.insert(clip->m_pcm.end(), chunk.begin(),
                               chunk.begin() + static_cast<std::ptrdiff_t>(read * clip->m_channels));
        }
    }
    ma_decoder_uninit(&decoder);

    if (clip->m_pcm.empty() || clip->m_channels == 0 || clip->m_sampleRate == 0) {
        Logger::error("AudioClip: 解码结果为空 {}", relPath.string());
        return nullptr;
    }
    Logger::info("AudioClip: 已解码 {}（{} 声道 / {} Hz / {:.2f} 秒）", relPath.string(),
                 clip->m_channels, clip->m_sampleRate,
                 static_cast<double>(clip->m_pcm.size()) / clip->m_channels / clip->m_sampleRate);
    return clip;
}

const std::filesystem::path& AudioClip::path() const { return m_path; }
const std::vector<float>& AudioClip::pcmF32() const { return m_pcm; }
uint32_t AudioClip::channels() const { return m_channels; }
uint32_t AudioClip::sampleRate() const { return m_sampleRate; }

}  // namespace NovaEngine
