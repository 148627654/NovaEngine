#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace NovaEngine {

// 解码后 PCM 资产（V0.3-2）：wav/mp3/flac/ogg 自动识别；v1 全量解码 f32（流式列后续）
class AudioClip {
public:
    // AssetStream 读取（pak 兼容）；失败返回 nullptr + Logger::error
    static std::shared_ptr<AudioClip> load(const std::filesystem::path& relPath);
    const std::filesystem::path& path() const;

    // 内部数据访问（AudioEngine 专用；公共头无 miniaudio）
    const std::vector<float>& pcmF32() const;     // 交错立体声/单声道 f32 样本
    uint32_t channels() const;
    uint32_t sampleRate() const;

private:
    AudioClip() = default;
    std::filesystem::path m_path;
    std::vector<float> m_pcm;
    uint32_t m_channels = 0;
    uint32_t m_sampleRate = 0;
    friend class AudioEngine;
};

}  // namespace NovaEngine
