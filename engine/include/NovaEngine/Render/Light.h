#pragma once
#include <glm/vec3.hpp>

namespace NovaEngine {

struct DirectionalLightData {
    glm::vec3 direction = glm::normalize(glm::vec3(0.5f, -1.0f, -0.3f));  // 指向光源
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    glm::vec3 ambient = glm::vec3(0.12f, 0.12f, 0.15f);
};

}  // namespace NovaEngine
