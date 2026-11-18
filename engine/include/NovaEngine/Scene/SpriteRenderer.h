#pragma once
#include <memory>
#include <string>
#include <glm/vec4.hpp>
#include <nlohmann/json_fwd.hpp>
#include <NovaEngine/Render/Texture.h>
#include "Component.h"

namespace NovaEngine {

// 2D 精灵渲染（V0.5-10）：世界空间 quad（尺寸 = 贴图像素 / pixelsPerUnit；2D 平面 = 世界 XY）。
// 渲染：透明段（AlphaBlend 固定），按 (sortingLayer, orderInLayer, 视距) 排序——层越大越后绘。
class SpriteRenderer : public Component {
public:
    explicit SpriteRenderer(GameObject* owner);
    std::string texturePath;               // 空 = 白块
    std::shared_ptr<Texture> texture = nullptr;
    glm::vec4 color{1.0f};
    bool flipX = false, flipY = false;
    int sortingLayer = 0;                  // v1 无命名层表
    int orderInLayer = 0;
    float pixelsPerUnit = 100.0f;
    void onSerialize(nlohmann::json& out) const override;
    void onDeserialize(const nlohmann::json& in) override;
};

}  // namespace NovaEngine
