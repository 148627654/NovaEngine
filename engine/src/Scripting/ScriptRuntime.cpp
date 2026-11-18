// ScriptRuntime.cpp —— 骨架日桩（V0.5-7）：接口冻结于 §25.5；LoadLibrary/重载实现归 c5（独占）。
// isLoaded()=false、全部入口安全 no-op（reload/load 返回 false；不打印错误——桩期静默，
// c5 实现后按契约失败路径 Logger::error）。
#include <NovaEngine/Scripting/ScriptRuntime.h>

namespace NovaEngine {

void ScriptRuntime::setDllPath(const std::filesystem::path& path) { (void)path; }
bool ScriptRuntime::load() { return false; }
void ScriptRuntime::unload() {}
bool ScriptRuntime::isLoaded() { return false; }

const std::string& ScriptRuntime::lastError() {
    static const std::string kEmpty;
    return kEmpty;
}

bool ScriptRuntime::reload(Scene& scene) {
    (void)scene;
    return false;
}

std::string ScriptRuntime::snapshotScriptFields(const Scene& scene) {
    (void)scene;
    return std::string();
}

}  // namespace NovaEngine
