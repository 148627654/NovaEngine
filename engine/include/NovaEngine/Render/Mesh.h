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

    // ===== V0.5-9 蒙皮（§25.7 冻结签名，骨架日 c0 布；实现归 c7 的 Mesh.cpp）=====
    void setBoneMatrices(const std::vector<glm::mat4>& mats);   // 懒建 UBO（binding 1，上限 64）；空 = 不绑定
    bool isSkinned() const;

private:
    Mesh() = default;
    uint32_t m_vao = 0, m_vbo = 0, m_ebo = 0;
    uint32_t m_indexCount = 0;
    AABB m_aabb;
    // 实例化 VAO/VBO（懒创建：drawInstanced 首次调用时构建；mutable——const 方法内懒初始化）
    mutable uint32_t m_instancedVao = 0;
    mutable uint32_t m_instancedVbo = 0;
    // 蒙皮状态（V0.5-9）：c7 在 Mesh.cpp 内维护（懒建骨骼 UBO binding 1 + 布局标记）
};

}  // namespace NovaEngine
