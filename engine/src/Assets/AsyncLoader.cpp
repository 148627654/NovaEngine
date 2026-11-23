// ===== V1.0-2 AsyncLoader（§27.1）：骨架日桩实现（2026-11-23）=====
// 桩语义 = 未启用时行为：init/shutdown no-op、入队返回 false、进度恒 1、代际恒 0。
// 真实实现归 cR（波 2）：单工作线程 + mutex/condition_variable 任务队列 + 代际取消 +
// 结果队列主线程 flush（GPU 上传回主线程铁律）。cR 直接在本文件填充实现，头文件
// 签名已冻结（§27.1），不得修改。
#include <NovaEngine/Assets/AsyncLoader.h>

namespace NovaEngine {

void AsyncLoader::init() {}
void AsyncLoader::shutdown() {}
bool AsyncLoader::loadSceneAsync(const std::string& /*name*/, bool /*additive*/) { return false; }
void AsyncLoader::flushPendingResults() {}
float AsyncLoader::progress() { return 1.0f; }
uint32_t AsyncLoader::generation() { return 0; }
void AsyncLoader::reset() {}

}  // namespace NovaEngine
