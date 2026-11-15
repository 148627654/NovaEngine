// Button.cpp —— 运行时 UI 按钮（V0.3-3）
// 含 Image 全部字段（组合而非继承）；onClick 由脚本赋值——C++ 内嵌脚本无 Inspector
// 事件绑定，onClick 不序列化（场景保存/恢复后脚本 onAwake/onStart 重新赋值）。
// 运行期输入状态（m_hovered/m_pressed/m_pressedEdge）由 UIRenderer::processInput
// 每帧覆写；hovered()/pressedThisFrame() 供脚本 onUpdate 查询。
#include <NovaEngine/Scene/Button.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

namespace NovaEngine {

namespace {

glm::vec2 jsonToVec2(const nlohmann::json& j, const glm::vec2& fallback) {
    if (!j.is_array() || j.size() < 2) return fallback;
    try {
        return glm::vec2(j[0].get<float>(), j[1].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

glm::vec4 jsonToVec4(const nlohmann::json& j, const glm::vec4& fallback) {
    if (!j.is_array() || j.size() < 4) return fallback;
    try {
        return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(),
                         j[3].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

Button::Button(GameObject* owner) : Component(owner) {}

bool Button::hovered() const { return m_hovered; }

bool Button::pressedThisFrame() const { return m_pressedEdge; }

void Button::onSerialize(nlohmann::json& out) const {
    out["color"] = nlohmann::json::array({color.x, color.y, color.z, color.w});
    out["hoverColor"] = nlohmann::json::array({hoverColor.x, hoverColor.y, hoverColor.z, hoverColor.w});
    out["pressedColor"] =
        nlohmann::json::array({pressedColor.x, pressedColor.y, pressedColor.z, pressedColor.w});
    out["slice9"] = nlohmann::json::array({slice9.x, slice9.y, slice9.z, slice9.w});
    out["anchor"] = nlohmann::json::array({anchor.x, anchor.y});
    out["offset"] = nlohmann::json::array({offset.x, offset.y});
    out["size"] = nlohmann::json::array({size.x, size.y});
    // onClick / m_hovered / m_pressed / m_pressedEdge 均为运行期状态，不写 JSON
}

void Button::onDeserialize(const nlohmann::json& in) {
    color = jsonToVec4(in.value("color", nlohmann::json()), glm::vec4(1.0f));
    hoverColor = jsonToVec4(in.value("hoverColor", nlohmann::json()), glm::vec4(0.9f));
    pressedColor = jsonToVec4(in.value("pressedColor", nlohmann::json()), glm::vec4(0.8f));
    slice9 = jsonToVec4(in.value("slice9", nlohmann::json()), glm::vec4(0.0f));
    anchor = jsonToVec2(in.value("anchor", nlohmann::json()), glm::vec2(0.5f));
    offset = jsonToVec2(in.value("offset", nlohmann::json()), glm::vec2(0.0f));
    size = jsonToVec2(in.value("size", nlohmann::json()), glm::vec2(100.0f));
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::Button)
