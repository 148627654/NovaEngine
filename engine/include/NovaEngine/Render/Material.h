#pragma once
#include <memory>
#include <string>
#include <glm/vec3.hpp>
#include "Texture.h"

namespace NovaEngine {

struct Material {
    glm::vec3 albedo = glm::vec3(0.8f);
    float roughness = 0.6f;
    float metallic = 0.0f;
    std::shared_ptr<Texture> albedoMap = nullptr;    // nullptr = 纯色（运行时 GPU 纹理）
    std::string albedoMapPath;                       // 贴图资产相对路径（序列化用，可为空）
};

}  // namespace NovaEngine
