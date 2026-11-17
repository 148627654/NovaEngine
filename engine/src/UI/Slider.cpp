// Slider.cpp —— 横向滑条（V0.5-13 v1）
// 渲染与输入全部在 UIRenderer（processInput 冒泡链命中后：按下即拖拽、拖拽中连续
// onValueChanged、松手停）。value 写回由 UIRenderer 直接改组件字段（Inspector live
// edit 语义一致）。onValueChanged 由脚本赋值，不序列化（同 Button::onClick 裁定）。
#include <NovaEngine/UI/Slider.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>

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

// 布尔字段读取：键缺失或类型不符回退默认值（畸形文件不崩溃）
bool jsonToBool(const nlohmann::json& j, const char* key, bool fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) return fallback;
    return j[key].get<bool>();
}

// 浮点字段读取：键缺失或类型不符回退默认值
float jsonToFloat(const nlohmann::json& j, const char* key, float fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return fallback;
    try {
        return j[key].get<float>();
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

Slider::Slider(GameObject* owner) : Component(owner) {}

bool Slider::dragging() const { return m_dragging; }

void Slider::onSerialize(nlohmann::json& out) const {
    out["value"] = value;
    out["minValue"] = minValue;
    out["maxValue"] = maxValue;
    out["wholeNumbers"] = wholeNumbers;
    out["backgroundColor"] =
        nlohmann::json::array({backgroundColor.x, backgroundColor.y, backgroundColor.z, backgroundColor.w});
    out["fillColor"] = nlohmann::json::array({fillColor.x, fillColor.y, fillColor.z, fillColor.w});
    out["handleColor"] = nlohmann::json::array({handleColor.x, handleColor.y, handleColor.z, handleColor.w});
    out["fillThickness"] = fillThickness;
    out["handleSize"] = nlohmann::json::array({handleSize.x, handleSize.y});
    out["anchor"] = nlohmann::json::array({anchor.x, anchor.y});
    out["offset"] = nlohmann::json::array({offset.x, offset.y});
    out["size"] = nlohmann::json::array({size.x, size.y});
    out["backgroundSpritePath"] = backgroundSpritePath;
    out["fillSpritePath"] = fillSpritePath;
    out["handleSpritePath"] = handleSpritePath;
    // onValueChanged / m_dragging 均为运行期状态，不写 JSON
}

void Slider::onDeserialize(const nlohmann::json& in) {
    minValue = jsonToFloat(in, "minValue", 0.0f);
    maxValue = jsonToFloat(in, "maxValue", 1.0f);
    wholeNumbers = jsonToBool(in, "wholeNumbers", false);
    // value 钳制 [min,max]（min>max 防御：按升序区间钳制）；wholeNumbers = 取整
    const float lo = std::min(minValue, maxValue), hi = std::max(minValue, maxValue);
    float v = std::clamp(jsonToFloat(in, "value", 0.5f), lo, hi);
    if (wholeNumbers) v = std::round(v);
    value = v;
    backgroundColor = jsonToVec4(in.value("backgroundColor", nlohmann::json()), glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
    fillColor = jsonToVec4(in.value("fillColor", nlohmann::json()), glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
    handleColor = jsonToVec4(in.value("handleColor", nlohmann::json()), glm::vec4(1.0f));
    fillThickness = jsonToFloat(in, "fillThickness", 6.0f);
    handleSize = jsonToVec2(in.value("handleSize", nlohmann::json()), glm::vec2(16.0f, 24.0f));
    anchor = jsonToVec2(in.value("anchor", nlohmann::json()), glm::vec2(0.5f));
    offset = jsonToVec2(in.value("offset", nlohmann::json()), glm::vec2(0.0f));
    size = jsonToVec2(in.value("size", nlohmann::json()), glm::vec2(200.0f, 30.0f));
    backgroundSpritePath = in.value("backgroundSpritePath", std::string());
    fillSpritePath = in.value("fillSpritePath", std::string());
    handleSpritePath = in.value("handleSpritePath", std::string());
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::Slider)
