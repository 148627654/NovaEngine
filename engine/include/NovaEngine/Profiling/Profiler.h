#pragma once
#include <cstddef>
#include <cstdint>

namespace NovaEngine {

// ===== V1.0-1 Profiler 完整版（§26.2 冻结接口，2026-11-20 骨架日布）=====
// 头文件签名冻结：实现归 cR（engine/src/Profiling/Profiler.cpp）；变更须报 Architect 重议。

// 帧记录定长数组上限（§26.2 冻结；命名空间级常量供 FrameRecord 使用）
constexpr int kMaxProfilerSections = 64;
constexpr int kMaxProfilerGpuPasses = 6;   // shadow/sky/opaque/transparent/post/ui

// 内存快照（V1.0-1；V1.0-6 填贴图压缩字节、V1.0-13 收口校准）
struct MemorySnapshot {
    size_t meshVertexBytes = 0;     // 顶点+索引字节（MeshData 上传时累计）
    size_t textureBytes = 0;        // GPU 贴图占用估算：未压缩 = w*h*4；压缩 = 块数据字节（Texture::sizeBytes 汇总）
    size_t audioBytes = 0;          // AudioClip 解码 PCM 字节（stream=true 的流式源不计入）
    size_t cubemapBytes = 0;
    size_t materialAssetCount = 0;
    size_t objectCount = 0, componentCount = 0;   // 活动场景统计
    size_t loadedSceneCount = 0;    // V1.0-2 落地后生效（波 1 恒 1）
};

// 帧记录（环形缓冲）
struct FrameRecord {
    uint64_t frameNumber = 0;
    float frameMs = 0.0f;
    float sectionMs[kMaxProfilerSections];      // 未上报 = -1
    float gpuPassMs[kMaxProfilerGpuPasses];     // 延迟 3 帧回读；未就绪 = -1
    MemorySnapshot memory;
};

class Profiler {
public:
    static constexpr uint32_t kCapacity = 600;   // 10 秒 @60FPS（sizeof(FrameRecord) 实现时自测并写入汇报）
    static constexpr int kMaxSections = kMaxProfilerSections;
    static constexpr int kMaxGpuPasses = kMaxProfilerGpuPasses;
    static void beginFrame();        // 帧首（Stats::beginFrame 之后）
    static void endFrame();          // 帧末（swapBuffers 前）
    static void pushSection(const char* name, float ms);   // 命名分段；同名累加；名字表帧末定型
    static const FrameRecord* frame(uint32_t index);       // 0 = 最旧；越界 nullptr
    static uint32_t frameCount();
    static uint32_t capacity();
    static void clear();
    static MemorySnapshot memorySnapshot();   // 转发 AssetDatabase::memorySnapshot + 活动场景统计
    static float lastOverheadMs();   // 自身开销自测（验收 <1ms 证据）
};

// ===== NE_GPU_SCOPE / NE_PROFILE_SCOPE 包裹（§26.0 冻结项 3；Profiler 关闭 = 零开销空操作）=====
// GpuScope：GL_TIME_ELAPSED query 成对提交（延迟 3 帧回读防管线停顿；不支持计时查询 → 段恒 0）
class GpuScope {
public:
    explicit GpuScope(const char* name);
    ~GpuScope();
    GpuScope(const GpuScope&) = delete;
    GpuScope& operator=(const GpuScope&) = delete;
};

// CpuScope：构造记录起点，析构 pushSection(name, 耗时)（NE_PROFILE_SCOPE 用）
class CpuScope {
public:
    explicit CpuScope(const char* name);
    ~CpuScope();
    CpuScope(const CpuScope&) = delete;
    CpuScope& operator=(const CpuScope&) = delete;
};

}  // namespace NovaEngine

// 宏需在命名空间外（作用域变量名拼接 __LINE__）
#define NE_GPU_SCOPE_CONCAT2(a, b) a##b
#define NE_GPU_SCOPE_CONCAT(a, b) NE_GPU_SCOPE_CONCAT2(a, b)
#define NE_GPU_SCOPE(name) \
    ::NovaEngine::GpuScope NE_GPU_SCOPE_CONCAT(_neGpuScope_, __LINE__)(name)

#define NE_PROFILE_SCOPE_CONCAT2(a, b) a##b
#define NE_PROFILE_SCOPE_CONCAT(a, b) NE_PROFILE_SCOPE_CONCAT2(a, b)
#define NE_PROFILE_SCOPE(name) \
    ::NovaEngine::CpuScope NE_PROFILE_SCOPE_CONCAT(_neCpuScope_, __LINE__)(name)
