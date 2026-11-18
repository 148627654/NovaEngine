#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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

// ===== V0.5-9 蒙皮动画（§25.7 冻结接口变更，骨架日 c0 布；追加式——非蒙皮路径零变化）=====

// 蒙皮顶点（V0.5-9）：独立布局，不并入 Vertex（非蒙皮路径零变化，ROADMAP §10-C2 兼容裁定）
struct SkinnedVertex {
    glm::vec3 position; glm::vec3 normal; glm::vec2 uv; glm::vec3 tangent;
    glm::ivec4 boneIds{0}; glm::vec4 boneWeights{0.0f};   // 权重和归一化由导入器保证
};
struct SkeletonBone { std::string name; int parent = -1; glm::mat4 inverseBind{1.0f}; };
struct Skeleton { std::vector<SkeletonBone> bones; };
struct AnimKey { float time = 0.0f; glm::vec3 position{0.0f}; glm::quat rotation{1,0,0,0}; glm::vec3 scale{1.0f}; };
struct AnimChannel { std::string boneName; std::vector<AnimKey> keys; };
struct AnimationClip { std::string name; float duration = 0.0f; float ticksPerSecond = 1.0f; std::vector<AnimChannel> channels; };

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    AABB aabb;
    std::string name;
    // V0.5-9 追加（非蒙皮网格留空，旧代码路径逐字节不变）：
    std::vector<SkinnedVertex> skinnedVertices;
    Skeleton skeleton;
    std::vector<AnimationClip> clips;
};

}  // namespace NovaEngine
