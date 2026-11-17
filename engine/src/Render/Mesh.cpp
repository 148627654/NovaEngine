#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>   // 先 glad 后 GLFW 是项目约定（GLFW_INCLUDE_NONE 已定义，顺序本身无冲突）

#include <cstddef>

namespace NovaEngine {

std::shared_ptr<Mesh> Mesh::create(const MeshData& data) {
    if (data.vertices.empty() || data.indices.empty()) {
        Logger::error("[Mesh] 创建失败：顶点或索引为空（{}）",
                      data.name.empty() ? std::string("未命名") : data.name);
        return nullptr;
    }

    auto mesh = std::shared_ptr<Mesh>(new Mesh());

    glGenVertexArrays(1, &mesh->m_vao);
    glBindVertexArray(mesh->m_vao);

    // 交错布局：location0=position / 1=normal / 2=uv / 3=tangent（与 blinn_phong.vert 一致）
    constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(Vertex));

    glGenBuffers(1, &mesh->m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
                 data.vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &mesh->m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
                 data.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride,
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride,
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                          reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, kStride,
                          reinterpret_cast<const void*>(offsetof(Vertex, tangent)));

    mesh->m_indexCount = static_cast<uint32_t>(data.indices.size());
    mesh->m_aabb = data.aabb;

    glBindVertexArray(0);
    return mesh;
}

Mesh::~Mesh() {
    if (m_instancedVao) glDeleteVertexArrays(1, &m_instancedVao);
    if (m_instancedVbo) glDeleteBuffers(1, &m_instancedVbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void Mesh::draw() const {
    if (!m_vao || m_indexCount == 0) return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::drawInstanced(const std::vector<glm::mat4>& models) const {
    if (models.empty() || !m_vao || m_indexCount == 0) return;   // 空向量 no-op

    // 懒建 per-Mesh instanced VAO：复用主 VBO/EBO（顶点属性 0..3 定义与主 VAO 一致），
    // 追加实例矩阵 VBO（location 4..7 mat4、divisor 1，与 blinn_phong_instanced.vert 对齐）
    if (m_instancedVao == 0) {
        glGenVertexArrays(1, &m_instancedVao);
        glBindVertexArray(m_instancedVao);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(Vertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(Vertex, uv)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(Vertex, tangent)));

        // mat4 = 4 个连续 vec4 属性（location 4..7），每实例推进一次（divisor 1）
        glGenBuffers(1, &m_instancedVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_instancedVbo);
        for (uint32_t i = 0; i < 4; ++i) {
            glEnableVertexAttribArray(4 + i);
            glVertexAttribPointer(4 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                  reinterpret_cast<const void*>(i * sizeof(glm::vec4)));
            glVertexAttribDivisor(4 + i, 1);
        }
        glBindVertexArray(0);
    }

    // 每次调用整体上传实例矩阵（流式：批规模 ≤512，帧内多次上传可接受）
    glBindVertexArray(m_instancedVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instancedVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(models.size() * sizeof(glm::mat4)),
                 models.data(), GL_STREAM_DRAW);
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(models.size()));
    glBindVertexArray(0);
}

const AABB& Mesh::aabb() const { return m_aabb; }

}  // namespace NovaEngine
