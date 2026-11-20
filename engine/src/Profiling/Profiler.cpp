// ===== V1.0-1 Profiler（§26.2）：骨架日桩实现（2026-11-20）=====
// 桩语义 = Profiler 关闭时的零开销默认：所有方法空操作。
// 真实实现归 cR（波 1）：帧环形缓冲、pushSection 累计、GPU timer 延迟回读、
// memorySnapshot 转发 AssetDatabase::memorySnapshot + 活动场景统计、lastOverheadMs 自测。
// cR 直接在本文件填充实现，头文件签名已冻结（§26.2），不得修改。
#include <NovaEngine/Profiling/Profiler.h>

namespace NovaEngine {

// 桩：无计时查询提交（关闭 = 零开销）
GpuScope::GpuScope(const char* /*name*/) {}
GpuScope::~GpuScope() {}

// 桩：无计时
CpuScope::CpuScope(const char* /*name*/) {}
CpuScope::~CpuScope() {}

void Profiler::beginFrame() {}
void Profiler::endFrame() {}
void Profiler::pushSection(const char* /*name*/, float /*ms*/) {}
const FrameRecord* Profiler::frame(uint32_t /*index*/) { return nullptr; }
uint32_t Profiler::frameCount() { return 0; }
uint32_t Profiler::capacity() { return kCapacity; }
void Profiler::clear() {}
MemorySnapshot Profiler::memorySnapshot() { return {}; }
float Profiler::lastOverheadMs() { return 0.0f; }

}  // namespace NovaEngine
