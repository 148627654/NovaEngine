#pragma once
#include <memory>
#include <string>
#include <glm/vec3.hpp>
#include "Texture.h"

namespace NovaEngine {

enum class BlendMode { Opaque, AlphaBlend };   // V0.2-8：渲染队列归属只看 blendMode；未来可加 Additive

struct Material {
    glm::vec3 albedo = glm::vec3(0.8f);
    float roughness = 0.6f;
    float metallic = 0.0f;
    std::shared_ptr<Texture> albedoMap = nullptr;    // nullptr = 纯色（运行时 GPU 纹理）
    std::string albedoMapPath;                       // 贴图资产相对路径（序列化用，可为空）
    float alpha = 1.0f;                              // AlphaBlend 时的混合系数
    BlendMode blendMode = BlendMode::Opaque;
};

}  // namespace NovaEngine
