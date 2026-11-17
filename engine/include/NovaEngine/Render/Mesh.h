#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/mat4x4.hpp>
#include "MeshData.h"

namespace NovaEngine {

class Mesh {
public:
    static std::shared_ptr<Mesh> create(const MeshData& data);   // 上传 VBO/EBO，构建 VAO
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;                       // bindVertexArray + glDrawElements(GL_TRIANGLES)
    // 实例化绘制（V0.5-14）：懒建 per-Mesh instanced VAO（复用 VBO/EBO + 实例矩阵 VBO，
    // attrib location 4..7 mat4、divisor 1）；每次调用上传矩阵后 glDrawElementsInstanced。
    // models 为空 no-op；GL 细节全部封装于 Mesh.cpp（公共头无泄漏）
    void drawInstanced(const std::vector<glm::mat4>& models) const;
    const AABB& aabb() const;                // 拾取与聚焦用
    uint32_t indexCount() const { return m_indexCount; }   // 统计用（V0.2-11）

private:
    Mesh() = default;
    uint32_t m_vao = 0, m_vbo = 0, m_ebo = 0;
    uint32_t m_indexCount = 0;
    AABB m_aabb;
    // 实例化 VAO/VBO（懒创建：drawInstanced 首次调用时构建；mutable——const 方法内懒初始化）
    mutable uint32_t m_instancedVao = 0;
    mutable uint32_t m_instancedVbo = 0;
};

}  // namespace NovaEngine
