#pragma once

namespace NovaEngine {

// ===== V1.0-1 Profiler 面板辅助接口（§26.2 补充，cR 波 1 新增头，2026-11-20）=====
// 背景：Profiler.h 为骨架日冻结接口（帧数据读取），FrameRecord 不含段名字符串
//（定长 float 数组 + 未上报 = -1 的设计）。段名表由 Profiler.cpp 持有（全局持久、
// 帧末定型，§26.2「同名累加」），面板经本头读取。实现位于 Profiler.cpp；
// 未纳入 NovaEngine.h 伞头（伞头变更归主会话），面板直接 include 本头。

// 段名表与 GPU 固定段名（纯读取；帧数据本身仍走 Profiler::frame）
namespace ProfilerSections {

// CPU 段名表：index ∈ [0, sectionCount())；空槽（未注册）返回 nullptr。
// 0 号槽为合并桶 "script:其他"（§26.2：kMaxSections=64 超出合并），未溢出时恒 -1 不上报。
const char* sectionName(int index);

// 已定型段名数（≤ Profiler::kMaxSections）
int sectionCount();

// GPU 6 段固定名（§26.2 冻结顺序）：shadow/sky/opaque/transparent/post/ui；越界 nullptr
const char* gpuPassName(int pass);

}  // namespace ProfilerSections

}  // namespace NovaEngine
