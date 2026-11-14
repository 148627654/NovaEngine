#include <NovaEngine/Render/Primitives.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace NovaEngine {

namespace {

// 由顶点计算 AABB
AABB computeAabb(const std::vector<Vertex>& vertices) {
    AABB box;
    for (const Vertex& v : vertices) {
        box.min = glm::min(box.min, v.position);
        box.max = glm::max(box.max, v.position);
    }
    return box;
}

// 追加立方体一个面（n = 外向法线，t = u 轴切线，逆时针环绕）
void addCubeFace(MeshData& out, const glm::vec3& n, const glm::vec3& t) {
    const glm::vec3 b = glm::cross(n, t);   // v 轴
    const glm::vec3 c = n * 0.5f;           // 面中心（立方体半边长 0.5）
    const glm::vec3 corners[4] = {
        c - t * 0.5f - b * 0.5f,
        c + t * 0.5f - b * 0.5f,
        c + t * 0.5f + b * 0.5f,
        c - t * 0.5f + b * 0.5f,
    };
    const glm::vec2 uvs[4] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };

    const uint32_t base = static_cast<uint32_t>(out.vertices.size());
    for (int i = 0; i < 4; ++i) {
        out.vertices.push_back(Vertex{corners[i], n, uvs[i], t});
    }
    const uint32_t idx[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
    out.indices.insert(out.indices.end(), idx, idx + 6);
}

}  // namespace

std::shared_ptr<Mesh> Primitives::cube() {
    MeshData data;
    data.name = "Cube";
    // 六个面（+X/-X/+Y/-Y/+Z/-Z），法线与切线保证 UV 连续
    addCubeFace(data, glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec3( 0.0f, 0.0f, -1.0f));
    addCubeFace(data, glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3( 0.0f, 0.0f,  1.0f));
    addCubeFace(data, glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec3( 1.0f, 0.0f,  0.0f));
    addCubeFace(data, glm::vec3( 0.0f, -1.0f, 0.0f), glm::vec3( 1.0f, 0.0f,  0.0f));
    addCubeFace(data, glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec3( 1.0f, 0.0f,  0.0f));
    addCubeFace(data, glm::vec3( 0.0f, 0.0f, -1.0f), glm::vec3(-1.0f, 0.0f,  0.0f));
    data.aabb = computeAabb(data.vertices);
    return Mesh::create(data);
}

std::shared_ptr<Mesh> Primitives::sphere(int segments) {
    // UV 球参数化：环向 segments 段，极向 rings 环（北极 φ=0 → 南极 φ=π，+Y 向上）
    segments = std::max(3, segments);
    const int rings = std::max(2, segments / 2);

    MeshData data;
    data.name = "Sphere";

    for (int ring = 0; ring <= rings; ++ring) {
        const float phi = glm::pi<float>() * static_cast<float>(ring) / static_cast<float>(rings);
        const float y = std::cos(phi);
        const float r = std::sin(phi);
        for (int seg = 0; seg <= segments; ++seg) {
            const float theta = glm::two_pi<float>() * static_cast<float>(seg) / static_cast<float>(segments);
            const glm::vec3 pos(r * std::cos(theta), y, r * std::sin(theta));
            // 切线 = 方位角切向（U 增大方向）；球心在原点时法线即位置方向
            const glm::vec3 tangent(-std::sin(theta), 0.0f, std::cos(theta));
            data.vertices.push_back(Vertex{
                pos,
                glm::normalize(pos),
                glm::vec2(static_cast<float>(seg) / static_cast<float>(segments),
                          static_cast<float>(ring) / static_cast<float>(rings)),
                tangent,
            });
        }
    }

    const int cols = segments + 1;
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            const uint32_t a = static_cast<uint32_t>(ring * cols + seg);
            const uint32_t b = static_cast<uint32_t>((ring + 1) * cols + seg);
            // 外向逆时针环绕（两极行退化为零面积三角形，不产生视觉伪影）
            data.indices.push_back(a);
            data.indices.push_back(a + 1);
            data.indices.push_back(b);
            data.indices.push_back(a + 1);
            data.indices.push_back(b + 1);
            data.indices.push_back(b);
        }
    }

    data.aabb = computeAabb(data.vertices);
    return Mesh::create(data);
}

std::shared_ptr<Mesh> Primitives::plane(float size) {
    MeshData data;
    data.name = "Plane";
    const float h = size * 0.5f;
    const glm::vec3 n(0.0f, 1.0f, 0.0f);
    const glm::vec3 t(1.0f, 0.0f, 0.0f);
    data.vertices = {
        Vertex{glm::vec3(-h, 0.0f, -h), n, glm::vec2(0.0f, 0.0f), t},
        Vertex{glm::vec3( h, 0.0f, -h), n, glm::vec2(1.0f, 0.0f), t},
        Vertex{glm::vec3( h, 0.0f,  h), n, glm::vec2(1.0f, 1.0f), t},
        Vertex{glm::vec3(-h, 0.0f,  h), n, glm::vec2(0.0f, 1.0f), t},
    };
    data.indices = { 0, 2, 1, 0, 3, 2 };   // 从 +Y 俯视逆时针
    data.aabb = computeAabb(data.vertices);
    return Mesh::create(data);
}

}  // namespace NovaEngine
