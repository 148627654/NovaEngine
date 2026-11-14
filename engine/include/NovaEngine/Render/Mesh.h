#pragma once
#include <cstdint>
#include <memory>
#include "MeshData.h"

namespace NovaEngine {

class Mesh {
public:
    static std::shared_ptr<Mesh> create(const MeshData& data);   // 上传 VBO/EBO，构建 VAO
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;                       // bindVertexArray + glDrawElements(GL_TRIANGLES)
    const AABB& aabb() const;                // 拾取与聚焦用
    uint32_t indexCount() const { return m_indexCount; }   // 统计用（V0.2-11）

private:
    Mesh() = default;
    uint32_t m_vao = 0, m_vbo = 0, m_ebo = 0;
    uint32_t m_indexCount = 0;
    AABB m_aabb;
};

}  // namespace NovaEngine
