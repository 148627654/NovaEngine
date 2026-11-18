// Animator.cpp —— 骨架日桩（V0.5-9）：接口冻结于 §25.7；状态机/过渡/骨骼上传实现归 c7（独占）。
// 脚本 API 安全 no-op；序列化最小往返（states/transitions/parameters 结构解析归 c7）。
#include <NovaEngine/Animation/Animator.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

Animator::Animator(GameObject* owner) : Component(owner) {}

void Animator::setFloat(const std::string& name, float v) { (void)name; (void)v; }
void Animator::setBool(const std::string& name, bool v) { (void)name; (void)v; }
void Animator::setTrigger(const std::string& name) { (void)name; }
float Animator::getFloat(const std::string& name) const { (void)name; return 0.0f; }

const std::string& Animator::currentStateName() const {
    static const std::string kEmpty;   // 骨架日空实现（无状态机）
    return kEmpty;
}

void Animator::play(const std::string& stateName) { (void)stateName; }

void Animator::onUpdate(float dt) { (void)dt; }

void Animator::onSerialize(nlohmann::json& out) const {
    out["defaultState"] = defaultState;
    out["states"] = nlohmann::json::array();
    out["transitions"] = nlohmann::json::array();
    out["parameters"] = nlohmann::json::array();
}

void Animator::onDeserialize(const nlohmann::json& in) {
    defaultState = in.value("defaultState", 0);
    // 骨架日桩：states/transitions/parameters 的结构解析归 c7（空 states → no-op 兜底）
}

}  // namespace NovaEngine

// 组件静态注册（§25.0 纪律 4；注册名 "Animator"）
NE_REGISTER_COMPONENT(NovaEngine::Animator)
