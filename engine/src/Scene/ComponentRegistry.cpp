#include <NovaEngine/Scene/ComponentRegistry.h>
#include <NovaEngine/Scene/AudioListener.h>
#include <NovaEngine/Scene/AudioSource.h>
#include <NovaEngine/Scene/Button.h>
#include <NovaEngine/Scene/Camera.h>
#include <NovaEngine/Scene/Canvas.h>
#include <NovaEngine/Scene/Collider.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/Image.h>
#include <NovaEngine/Scene/MeshRenderer.h>
#include <NovaEngine/Scene/PrefabInstance.h>
#include <NovaEngine/Scene/Rigidbody.h>
#include <NovaEngine/Scene/Text.h>
#include <NovaEngine/Scene/Transform.h>
#include <NovaEngine/UI/Slider.h>
#include <NovaEngine/UI/ScrollView.h>
#include <NovaEngine/Core/Logger.h>
#include <algorithm>

namespace NovaEngine {

namespace {

// 注册名裁剪：名字含 "::" 时取最后一段
// （NE_REGISTER_COMPONENT(NovaEngine::Transform) → 注册名 "Transform"）
std::string trimTypeName(const std::string& name) {
    const auto pos = name.rfind("::");
    return (pos == std::string::npos) ? name : name.substr(pos + 2);
}

}  // namespace

std::unordered_map<std::string, ComponentRegistry::Entry>& ComponentRegistry::entries() {
    // 函数内静态：注册发生在静态初始化期，先于任何查询，规避跨翻译单元初始化顺序问题
    static std::unordered_map<std::string, Entry> s_entries;
    return s_entries;
}

void ComponentRegistry::registerBuiltin(const std::string& typeName, uint32_t typeId, Factory factory) {
    const std::string name = trimTypeName(typeName);
    auto& map = entries();
    if (map.contains(name)) {
        // 幂等：首个注册生效，重复注册同名仅告警不覆盖（保持 SceneSerializer 分发行为稳定）
        Logger::warn("组件注册表：内置组件 \"{}\" 已注册，忽略重复注册", name);
        return;
    }
    map.emplace(name, Entry{typeId, std::move(factory)});
}

std::unique_ptr<Component> ComponentRegistry::create(const std::string& typeName, GameObject* owner) {
    // 静态库链接裁剪防护（V0.3 波 0 回归修复）：内置组件经各自 .cpp 末尾的
    // NE_REGISTER_COMPONENT 自注册，但 NovaEngine 是静态库——若某二进制（如
    // NovaPlayer）未直接引用某组件的任何符号，MSVC 链接器不会把该组件 .obj 从
    // .lib 拉入，注册初始器随之缺失，SceneSerializer 反序列化该内置组件时报
    // "未知组件类型" 并跳过（实测 Transform 因 GameObject 直引而幸存，Camera /
    // DirectionalLight / MeshRenderer 因此丢失）。
    // 以下条件经 volatile 变量读取（编译期不可判定、运行期恒假），保证对四个内置
    // 组件构造函数（仅定义于各自 .cpp）的引用保留在本 .obj 中：链接器为解析引用
    // 必从静态库拉入对应组件 .obj，其自注册初始器随之生效；编辑器和 Player 均受益。
    // 新增内置组件时须同步追加此列表。
    static volatile bool s_linkGuardProbe = false;  // 永不被改写
    if (s_linkGuardProbe) {
        (void)std::make_unique<Transform>(owner);
        (void)std::make_unique<MeshRenderer>(owner);
        (void)std::make_unique<Camera>(owner);
        (void)std::make_unique<DirectionalLight>(owner);
        (void)std::make_unique<AudioSource>(owner);    // V0.3-2（Coder C 波 2）
        (void)std::make_unique<AudioListener>(owner);  // V0.3-2（Coder C 波 2）
        (void)std::make_unique<Canvas>(owner);         // V0.3-3（Coder B 波 2 UI）
        (void)std::make_unique<Image>(owner);          // V0.3-3（Coder B 波 2 UI）
        (void)std::make_unique<Text>(owner);           // V0.3-3（Coder B 波 2 UI）
        (void)std::make_unique<Button>(owner);         // V0.3-3（Coder B 波 2 UI）
        (void)std::make_unique<Rigidbody>(owner);      // V0.3-1（Coder A 波 2 物理）
        (void)std::make_unique<BoxCollider>(owner);    // V0.3-1（Coder A 波 2 物理）
        (void)std::make_unique<SphereCollider>(owner); // V0.3-1（Coder A 波 2 物理）
        (void)std::make_unique<CapsuleCollider>(owner);// V0.3-1（Coder A 波 2 物理）
        (void)std::make_unique<MeshCollider>(owner);   // V0.3-1（Coder A 波 2 物理）
        (void)std::make_unique<PrefabInstance>(owner); // V0.3-9（Coder B 波 4 Prefab）
        (void)std::make_unique<Slider>(owner);         // V0.5-13（c3 UI 控件）
        (void)std::make_unique<ScrollView>(owner);     // V0.5-13（c3 UI 控件）
    }

    auto& map = entries();
    auto it = map.find(trimTypeName(typeName));
    if (it == map.end())
        return nullptr;  // 未知组件返回 nullptr（调用方负责回退/告警）
    return it->second.factory(owner);
}

uint32_t ComponentRegistry::typeIdForName(const std::string& typeName) {
    auto& map = entries();
    auto it = map.find(trimTypeName(typeName));
    return (it == map.end()) ? 0u : it->second.typeId;
}

bool ComponentRegistry::exists(const std::string& typeName) {
    return entries().contains(trimTypeName(typeName));
}

std::vector<std::string> ComponentRegistry::builtinNames() {
    // Inspector「Add Component」下拉列表用；排序保证顺序稳定
    std::vector<std::string> names;
    auto& map = entries();
    names.reserve(map.size());
    for (const auto& kv : map)
        names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace NovaEngine
