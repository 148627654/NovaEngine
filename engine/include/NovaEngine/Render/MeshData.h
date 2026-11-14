#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace NovaEngine {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;
};

struct AABB {
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(-std::numeric_limits<float>::max());
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    AABB aabb;
    std::string name;
};

}  // namespace NovaEngine
