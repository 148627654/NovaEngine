#include <NovaEngine/Scripting/ScriptRegistry.h>
#include <algorithm>

namespace NovaEngine {

namespace {
// 注册名裁剪：名字含 "::" 时取最后一段
// （NE_REGISTER_SCRIPT(NovaGame::SpinningCube) → 注册名 "SpinningCube"）
std::string trimScriptName(const std::string& name) {
    const auto pos = name.rfind("::");
    return (pos == std::string::npos) ? name : name.substr(pos + 2);
}
}  // namespace

std::unordered_map<std::string, ScriptRegistry::ScriptFactory>& ScriptRegistry::factories() {
    // 函数内静态：避免跨翻译单元静态初始化顺序问题
    static std::unordered_map<std::string, ScriptFactory> s_factories;
    return s_factories;
}

void ScriptRegistry::registerScript(const std::string& name, ScriptFactory factory) {
    factories()[trimScriptName(name)] = std::move(factory);
}

std::unique_ptr<Behaviour> ScriptRegistry::create(const std::string& name, GameObject* owner) {
    auto& map = factories();
    auto it = map.find(trimScriptName(name));
    if (it == map.end())
        return nullptr;  // 未知脚本返回 nullptr（调用方负责告警）
    return it->second(owner);
}

bool ScriptRegistry::exists(const std::string& name) {
    return factories().contains(trimScriptName(name));
}

void ScriptRegistry::unregisterScript(const std::string& name) {
    factories().erase(trimScriptName(name));   // 不存在时静默（erase 幂等，§25.5）
}

void ScriptRegistry::removeAllScripts() {
    factories().clear();   // DLL 卸载前清空（防旧工厂悬垂，§25.5）
}

std::vector<std::string> ScriptRegistry::allNames() {
    // Inspector「Add Component」下拉列表用；排序保证顺序稳定
    std::vector<std::string> names;
    names.reserve(factories().size());
    for (const auto& [name, factory] : factories())
        (void)factory, names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace NovaEngine
