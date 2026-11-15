// Canvas.cpp —— 运行时 UI 子树根组件（V0.3-3）
// 屏幕空间恒全屏：Transform 忽略（UIRenderer 只读 uiScale 与子节点树）
#include <NovaEngine/Scene/Canvas.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

Canvas::Canvas(GameObject* owner) : Component(owner) {}

void Canvas::onSerialize(nlohmann::json& out) const {
    out["uiScale"] = uiScale;
}

void Canvas::onDeserialize(const nlohmann::json& in) {
    // in.value(key, 默认) 兜底旧 JSON；畸形字段异常由 SceneSerializer 统一 try/catch 接住
    uiScale = in.value("uiScale", 1.0f);
}

}  // namespace NovaEngine

// 组件静态注册（V0.3 波 0；注册名经 ComponentRegistry 裁剪为 "Canvas"）
NE_REGISTER_COMPONENT(NovaEngine::Canvas)
