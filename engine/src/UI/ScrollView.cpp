// ScrollView.cpp —— 滚动容器（V0.5-13 v1）
// 渲染（scissor 裁剪 + 内容平移 + 指示条）与滚轮滚动全部在 UIRenderer；
// m_contentSize 由 UIRenderer 遍历期重算（子物体 UI rect 并集，逻辑像素），
// contentSize() 仅供脚本查询——本组件不自行排版。
#include <NovaEngine/UI/ScrollView.h>

#include <NovaEngine/Scene/ComponentRegistry.h>

#include <nlohmann/json.hpp>

#include <algorithm>
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

// vec4 颜色字段读取：键缺失或类型不符回退默认值
glm::vec4 jsonToVec4(const nlohmann::json& j, const char* key, const glm::vec4& fallback) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_array() || j[key].size() < 4)
        return fallback;
    try {
        return glm::vec4(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(),
                         j[key][3].get<float>());
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace

ScrollView::ScrollView(GameObject* owner) : Component(owner) {}

glm::vec2 ScrollView::contentSize() const { return m_contentSize; }

void ScrollView::onSerialize(nlohmann::json& out) const {
    out["anchor"] = nlohmann::json::array({anchor.x, anchor.y});
    out["offset"] = nlohmann::json::array({offset.x, offset.y});
    out["size"] = nlohmann::json::array({size.x, size.y});
    out["scrollHorizontal"] = scrollHorizontal;
    out["scrollVertical"] = scrollVertical;
    out["scrollSensitivity"] = scrollSensitivity;
    out["scrollOffset"] = nlohmann::json::array({scrollOffset.x, scrollOffset.y});
    out["bgColor"] = nlohmann::json::array({bgColor.r, bgColor.g, bgColor.b, bgColor.a});
    out["borderColor"] =
        nlohmann::json::array({borderColor.r, borderColor.g, borderColor.b, borderColor.a});
    out["borderThickness"] = borderThickness;
    // m_contentSize 为运行期缓存，不写 JSON
}

void ScrollView::onDeserialize(const nlohmann::json& in) {
    anchor = jsonToVec2(in.value("anchor", nlohmann::json()), glm::vec2(0.5f));
    offset = jsonToVec2(in.value("offset", nlohmann::json()), glm::vec2(0.0f));
    size = jsonToVec2(in.value("size", nlohmann::json()), glm::vec2(300.0f, 200.0f));
    scrollHorizontal = jsonToBool(in, "scrollHorizontal", false);
    scrollVertical = jsonToBool(in, "scrollVertical", true);
    scrollSensitivity = std::max(jsonToFloat(in, "scrollSensitivity", 20.0f), 0.0f);   // 负值会反向滚动，钳 0
    // 负偏移防御性钳 0（正向钳制 [0, content-view] 由 UIRenderer 在 contentSize
    // 已知后执行；反序列化时内容尚未布局，仅保证非负）
    scrollOffset = jsonToVec2(in.value("scrollOffset", nlohmann::json()), glm::vec2(0.0f));
    scrollOffset.x = std::max(scrollOffset.x, 0.0f);
    scrollOffset.y = std::max(scrollOffset.y, 0.0f);
    // 外框（V0.5 验收补强）：旧文件缺字段回退默认值，不告警（与 Image/Text 扩展字段同策）
    bgColor = jsonToVec4(in, "bgColor", glm::vec4(0.10f, 0.11f, 0.13f, 0.85f));
    borderColor = jsonToVec4(in, "borderColor", glm::vec4(0.42f, 0.45f, 0.52f, 1.0f));
    borderThickness = std::max(jsonToFloat(in, "borderThickness", 1.5f), 0.0f);
}

}  // namespace NovaEngine

NE_REGISTER_COMPONENT(NovaEngine::ScrollView)
