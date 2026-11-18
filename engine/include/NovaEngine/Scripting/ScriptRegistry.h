#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Behaviour.h"

namespace NovaEngine {

class ScriptRegistry {
public:
    using ScriptFactory = std::function<std::unique_ptr<Behaviour>(GameObject*)>;
    // 若名字含 "::"（如宏传入 NovaGame::SpinningCube），注册名取最后一段（"SpinningCube"）
    static void registerScript(const std::string& name, ScriptFactory factory);
    static std::unique_ptr<Behaviour> create(const std::string& name, GameObject* owner);  // 未知返回 nullptr
    static bool exists(const std::string& name);
    static std::vector<std::string> allNames();     // Inspector「Add Component」下拉列表用
    static void unregisterScript(const std::string& name);   // 追加（V0.5-7）：单个移除（不存在静默）
    static void removeAllScripts();                          // 追加（V0.5-7）：DLL 卸载前清空（防旧工厂悬垂）

private:
    static std::unordered_map<std::string, ScriptFactory>& factories();  // 函数内静态，避免初始化顺序问题
};

}  // namespace NovaEngine

// 脚本 .cpp 文件末尾放置（注册宏）。
// 变量名不能拼 ClassName（限定名 NovaGame::SpinningCube 会展开成
// NovaGame::SpinningCube_s_registered 这种非法嵌套名），也不能拼 __LINE__/__COUNTER__
// （MSVC 一致模式预处理器 /Zc:preprocessor 下会生成非法 token 流）。
// 匿名命名空间 + 静态注册结构：每个 .cpp 内名字唯一，跨翻译单元天然不冲突
#define NE_REGISTER_SCRIPT(ClassName) \
    namespace { \
    struct NE_ScriptRegistrar { \
        NE_ScriptRegistrar() { \
            ::NovaEngine::ScriptRegistry::registerScript(#ClassName, \
                [](::NovaEngine::GameObject* go) { return std::make_unique<ClassName>(go); }); \
        } \
    }; \
    const NE_ScriptRegistrar NE_s_registrar_instance; \
    }
