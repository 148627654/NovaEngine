#pragma once
#include <filesystem>
#include <string>

namespace NovaEngine {

class Scene;

// 脚本 DLL 运行时（V0.5-7）：NovaScripts OBJECT 库 → NovaGame.dll（SHARED）。
class ScriptRuntime {
public:
    static void setDllPath(const std::filesystem::path& path);   // 启动时注入（编译期路径）
    static bool load();                                          // LoadLibrary；失败 false + Logger::error
    static void unload();                                        // FreeLibrary（正常路径走 reload）
    static bool isLoaded();
    static const std::string& lastError();

    // 重载核心：快照脚本字段（复用 onSerialize）→ 逐脚本组件替换实例（旧 vtable 实例
    // 不可在 FreeLibrary 后存活）→ 卸载旧 DLL → 加载新 DLL（静态注册器自动重新填充
    // ScriptRegistry）→ 按「对象 ID + 组件下标」原位恢复字段（onDeserialize）。
    // 任一步失败（加载失败/工厂缺失）→ 保留旧 DLL 继续运行，返回 false + error 日志。
    static bool reload(Scene& scene);

    // 字段快照（内存 JSON，不落盘）：每脚本组件 {type, enabled, fields（onSerialize 产物）}
    static std::string snapshotScriptFields(const Scene& scene);
};

}  // namespace NovaEngine
