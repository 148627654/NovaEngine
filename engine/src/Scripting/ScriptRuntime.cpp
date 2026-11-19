// ScriptRuntime.cpp —— 脚本 DLL 热重载实现（V0.5-7，c5）
//
// 符号方向（§25.5 冻结）：引擎代码静态链入 exe（/WHOLEARCHIVE + 从 NovaEngine.lib
// 生成 .def 导出全部引擎符号——WINDOWS_EXPORT_ALL_SYMBOLS 不扫静态库成员，见
// editor/player CMakeLists）；NovaGame.dll 链接 exe 导入库 → 脚本类的引擎引用
// （vtable/RTTI/函数）全部绑定到「加载它的 exe」的导出，每进程单一引擎实例。
//
// 重载核心（契约算法在冻结接口下的落地）：
//   快照（旧 DLL 存活，onSerialize 走旧 vtable）→ 清空旧工厂 → 整场景 JSON 重建
//   （旧实例在 FreeLibrary 前安全虚析构；注册表空 → 脚本组件跳过）→ FreeLibrary
//   → LoadLibrary 新 DLL（静态注册器自动重填注册表）→ 再次整场景重建
//   （新工厂创建 → 按「对象 ID + 组件数组下标」原位恢复字段，即 JSON 数组顺序）。
// 为何是整场景重建而非逐组件替换：冻结的 GameObject 接口无「按指针移除组件」API
//   （removeComponent<T> 按 typeId 匹配，反序列化挂载的脚本组件 typeId=0 不可命中；
//   InspectorPanel 移除脚本组件已有同款「整场景 JSON 重建」变通先例）。
// 失败路径：新 DLL 加载失败 → LoadLibrary 备份副本（NovaGame.dll.old，文件名不同可
//   加载第二实例，静态注册器重跑恢复旧工厂）→ 旧 DLL 继续运行，返回 false。
#include <windows.h>   // LoadLibraryW/FreeLibrary/GetLastError（须在最前）

#include <NovaEngine/Scripting/ScriptRuntime.h>

#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Serialization/SceneSerializer.h>
#include <NovaEngine/Scripting/Behaviour.h>
#include <NovaEngine/Scripting/ScriptRegistry.h>

#include <nlohmann/json.hpp>

#include "../Serialization/SerializationInternals.h"   // scriptTypeName：脚本类型名裁剪单一来源

#include <format>
#include <vector>

namespace NovaEngine {

namespace {

// 运行时状态（接口冻结为静态方法，状态放匿名命名空间，仅本 .cpp 可见）
std::filesystem::path s_dllPath;
HMODULE s_handle = nullptr;
std::string s_lastError;
std::filesystem::path s_backupPath;   // 最近一次 reload 的旧 DLL 备份（失败回滚用）

// 场景内脚本组件条目（「对象 ID + 组件下标」定位；type/enabled/fields 为快照内容）
struct ScriptEntry {
    uint64_t goId = 0;
    size_t index = 0;   // 该 GameObject 组件数组中的下标
    std::string type;
    bool enabled = true;
    nlohmann::json fields;
};

// 收集场景全部脚本组件条目（深度优先 × 组件数组下标序，与序列化顺序一致）。
// 调用前提：持有脚本实例 vtable 的 DLL 必须仍加载（onSerialize 虚调用）。
std::vector<ScriptEntry> collectScriptEntries(const Scene& scene) {
    std::vector<ScriptEntry> out;
    scene.forEachObject([&](GameObject* go) {
        const std::vector<Component*> comps = go->getComponents<Component>();
        for (size_t i = 0; i < comps.size(); ++i) {
            Behaviour* b = dynamic_cast<Behaviour*>(comps[i]);
            if (!b)
                continue;
            ScriptEntry e;
            e.goId = go->id();
            e.index = i;
            e.type = SerializationInternals::scriptTypeName(b);
            e.enabled = b->enabled;
            b->onSerialize(e.fields);
            out.push_back(std::move(e));
        }
    });
    return out;
}

size_t countFieldEntries(const std::vector<ScriptEntry>& entries) {
    size_t n = 0;
    for (const auto& e : entries)
        if (e.fields.is_object())
            n += e.fields.size();
    return n;
}

// LoadLibrary + 触发静态初始化器（NE_REGISTER_SCRIPT 自动注册进 ScriptRegistry）。
// 成功返回模块句柄；失败返回 nullptr 并填充错误描述。
HMODULE loadDllAndRegister(const std::filesystem::path& path, std::string& errOut) {
    HMODULE h = LoadLibraryW(path.wstring().c_str());
    if (!h)
        errOut = std::format("LoadLibrary 失败({}): Win32 错误 {}", path.string(),
                             static_cast<unsigned long>(GetLastError()));
    return h;
}

}  // namespace

void ScriptRuntime::setDllPath(const std::filesystem::path& path) {
    s_dllPath = path;
}

bool ScriptRuntime::load() {
    if (s_handle != nullptr)
        return true;   // 已加载：幂等，不重复注册
    if (s_dllPath.empty()) {
        s_lastError = "未设置脚本 DLL 路径";
        Logger::error("[ScriptRuntime] {}", s_lastError);
        return false;
    }
    s_handle = loadDllAndRegister(s_dllPath, s_lastError);
    if (s_handle == nullptr) {
        Logger::error("[ScriptRuntime] {}", s_lastError);
        return false;
    }
    Logger::info("[ScriptRuntime] 脚本 DLL 已加载: {}（注册脚本 {} 个）", s_dllPath.string(),
                 ScriptRegistry::allNames().size());
    return true;
}

void ScriptRuntime::unload() {
    // 铁律：FreeLibrary 前先清空注册表（防旧工厂 lambda 悬垂）；调用方须保证
    // 场景中已无脚本组件实例（正常路径走 reload；进程退出无需调用——OS 回收模块）
    ScriptRegistry::removeAllScripts();
    if (s_handle != nullptr) {
        FreeLibrary(s_handle);
        s_handle = nullptr;
    }
    s_backupPath.clear();
}

bool ScriptRuntime::isLoaded() {
    return s_handle != nullptr;
}

const std::string& ScriptRuntime::lastError() {
    return s_lastError;
}

std::string ScriptRuntime::snapshotScriptFields(const Scene& scene) {
    // 字段快照（内存 JSON，不落盘）：每脚本组件 {id, index, type, enabled, fields}
    // fields = onSerialize 产物；id+index 供「对象 ID + 组件下标」原位恢复定位
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : collectScriptEntries(scene)) {
        nlohmann::json j;
        j["id"] = e.goId;
        j["index"] = e.index;
        j["type"] = e.type;
        j["enabled"] = e.enabled;
        j["fields"] = e.fields;
        arr.push_back(std::move(j));
    }
    return arr.dump();
}

bool ScriptRuntime::reload(Scene& scene) {
    if (!isLoaded() && !load())
        return false;   // 未加载先尝试加载（首次编译产出后的自动触发路径；失败已记 error）

    // 1. 快照（旧 DLL 存活：onSerialize 走旧 vtable；JSON 往返承载全部脚本字段与场景状态）
    std::string snapshotJson;
    try {
        snapshotJson = SceneSerializer::serialize(scene);
    } catch (const std::exception& e) {
        s_lastError = std::format("场景快照失败: {}", e.what());
        Logger::error("[ScriptRuntime] 热重载中止: {}", s_lastError);
        return false;
    }
    const std::vector<ScriptEntry> before = collectScriptEntries(scene);

    // 2. 备份旧 DLL（失败回滚用；DLL 已加载不锁文件读）。备份失败仅 warn 降级（无回滚能力）
    s_backupPath = s_dllPath;
    s_backupPath += ".old";
    {
        std::error_code ec;
        std::filesystem::copy_file(s_dllPath, s_backupPath,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            Logger::warn("[ScriptRuntime] 旧 DLL 备份失败（{}），加载失败时无法回滚", ec.message());
            s_backupPath.clear();
        }
    }

    // 3. 清空旧工厂（防悬垂；同时令下方中间重建跳过脚本组件）
    ScriptRegistry::removeAllScripts();

    if (before.empty()) {
        // 场景无脚本组件：无需整场景重建，直接换 DLL + 换注册表
    } else {
        // 4. 中间重建：旧 DLL 仍加载 → 旧脚本实例安全虚析构；注册表空 → 脚本组件被跳过
        //   （反序列化对未知类型 warn 跳过，属既有行为）→ 场景此时零脚本组件
        SceneSerializer::deserialize(scene, snapshotJson);
        if (!collectScriptEntries(scene).empty()) {
            // 重建未生效（JSON 解析失败等）：场景保持原样、旧实例仍在、旧 DLL 仍加载。
            // 恢复注册表后安全返回（LoadLibrary 备份副本 → 静态注册器重跑）
            s_lastError = "中间重建失败，场景保持不变";
            Logger::error("[ScriptRuntime] 热重载中止: {}", s_lastError);
            std::string ignoredErr;
            if (!s_backupPath.empty() && loadDllAndRegister(s_backupPath, ignoredErr) != nullptr)
                Logger::info("[ScriptRuntime] 已从备份恢复脚本注册表，旧 DLL 继续运行");
            else
                Logger::error("[ScriptRuntime] 脚本注册表为空（下次 reload 可自愈）");
            return false;
        }
    }

    // 5. 卸载旧 DLL（此刻场景内已无旧 vtable 实例，铁律满足）
    FreeLibrary(s_handle);
    s_handle = nullptr;

    // 6. 加载新 DLL（静态注册器自动重新填充 ScriptRegistry）
    s_handle = loadDllAndRegister(s_dllPath, s_lastError);
    if (s_handle == nullptr) {
        Logger::error("[ScriptRuntime] 热重载失败: {}", s_lastError);
        // 回滚：备份副本是旧代码的另一实例（文件名不同 → 可加载；注册器重跑恢复旧注册表）
        if (!s_backupPath.empty()) {
            std::string ignoredErr;
            if (loadDllAndRegister(s_backupPath, ignoredErr) != nullptr) {
                Logger::info("[ScriptRuntime] 已回滚旧 DLL（备份副本），场景字段经重建恢复");
                if (!before.empty())
                    SceneSerializer::deserialize(scene, snapshotJson);   // 旧工厂恢复脚本组件+字段
                s_backupPath.clear();
                return false;
            }
            Logger::error("[ScriptRuntime] 备份副本加载亦失败，场景脚本组件缺失（场景其余正常）");
        } else {
            Logger::error("[ScriptRuntime] 无旧 DLL 备份，场景脚本组件缺失（场景其余正常）");
        }
        return false;
    }

    // 7. 重建：新工厂创建脚本组件，按「对象 ID + 组件数组下标」原位恢复字段（JSON 数组顺序）
    SceneSerializer::deserialize(scene, snapshotJson);
    const std::vector<ScriptEntry> after = collectScriptEntries(scene);
    if (after.size() < before.size())
        Logger::warn("[ScriptRuntime] {} 个脚本组件在新 DLL 中无对应工厂，已丢弃",
                     before.size() - after.size());

    Logger::info("[ScriptRuntime] 脚本热重载成功: 脚本组件 {} 个, 字段恢复 {} 个",
                 after.size(), countFieldEntries(after));
    s_lastError.clear();
    s_backupPath.clear();
    return true;
}

}  // namespace NovaEngine
