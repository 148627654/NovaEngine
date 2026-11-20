#include <NovaEngine/Render/Mesh.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/Renderer.h>   // isInitialized（V1.0 波 1 骨架日 GL 守卫）
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>   // 先 glad 后 GLFW 是项目约定（GLFW_INCLUDE_NONE 已定义，顺序本身无冲突）

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace NovaEngine {

namespace {

// ===== V0.5-9 蒙皮（§25.7）：Mesh.cpp 内部维护（Mesh.h 冻结——成员不动，状态落本文件旁表）=====
constexpr size_t kMaxBones = 64;   // 骨骼矩阵 UBO 上限（§25.7 冻结）

struct SkinnedState {
    uint32_t boneUbo = 0;   // 懒建骨骼矩阵 UBO（binding 1）
};

// Mesh* → 蒙皮状态旁表（create 建、析构清；渲染单线程，无锁）
std::unordered_map<const Mesh*, SkinnedState>& skinnedStates() {
    static std::unordered_map<const Mesh*, SkinnedState> s;
    return s;
}

// 蒙皮 program 懒建（blinn_phong_skinned.vert + 复用 blinn_phong.frag → 自动获得 IBL）。
// 只在 draw 期间调用（GL 上下文存活）；失败 null → 蒙皮网格回退普通 program（绑定姿势）。
std::shared_ptr<Shader>& skinnedShader() {
    static std::shared_ptr<Shader> s;
    static bool s_attempted = false;
    if (!s && !s_attempted) {
        s_attempted = true;
        s = Shader::createFromFiles("assets/shaders/blinn_phong_skinned.vert",
                                    "assets/shaders/blinn_phong.frag");
    }
    return s;
}

// 需要从当前 blinn_phong program 复制到蒙皮 program 的逐项 uniform（名 + GL 类型标记）。
// 复制原因：Renderer::setBlinnPhongUniforms 只作用于其私有 s_blinnPhong（共享文件冻结，
// c7 不得触碰）；灯光/材质/IBL/阴影 uniform 值经 glGetUniform* 读出 → glUniform* 写回蒙皮
// program（GL 4.5 合法；采样器 unit 为全局 GL 状态，绑定跨 program 共享，无需复制纹理）。
// 纹理采样器 uniform 恒为固定 unit 编号，直接写常量即可。
struct UniformSpec {
    const char* name;
    uint8_t type;   // 0=float 1=vec3 2=mat4 3=int
};
constexpr UniformSpec kCopiedUniforms[] = {
    {"uModel", 2}, {"uLightDir", 1}, {"uLightColor", 1}, {"uAmbient", 1},
    {"uAlbedo", 1}, {"uRoughness", 0}, {"uMetallic", 0}, {"uAlpha", 0},
    {"uUseAlbedoMap", 3}, {"uUseNormalMap", 3}, {"uUseMetallicMap", 3},
    {"uUseRoughnessMap", 3}, {"uUseEmissionMap", 3},
    {"uEmissionColor", 1}, {"uEmissionIntensity", 0},
    {"uGamma", 0}, {"uIblSkyRotation", 0},
    {"uShadowEnabled", 3}, {"uLightViewProj", 2}, {"uShadowBias", 0}, {"uShadowMapSize", 0},
    {"uIblIntensity", 0},
    {"uGbufferPass", 3}, {"uView", 2},
};

// 把当前绑定 program 的逐项 uniform 复制到蒙皮 program。
// 关键顺序：先绑定目标 program——glUniform* 只作用于当前绑定 program（Shader::setXXX
// 内部不绑定）；glGetUniform*(program, …) 带显式 program 参数，不受当前绑定影响。
void copyUniformsToSkinned(GLuint fromProgram, Shader& to) {
    to.bind();
    for (const UniformSpec& spec : kCopiedUniforms) {
        const GLint loc = glGetUniformLocation(fromProgram, spec.name);
        if (loc < 0) continue;   // 当前 program 无此 uniform（如旁路 shader）→ 跳过
        switch (spec.type) {
            case 0: {
                float v = 0.0f;
                glGetUniformfv(fromProgram, loc, &v);
                to.setFloat(spec.name, v);
                break;
            }
            case 1: {
                float v[3] = {0.0f, 0.0f, 0.0f};
                glGetUniformfv(fromProgram, loc, v);
                to.setVec3(spec.name, glm::vec3(v[0], v[1], v[2]));
                break;
            }
            case 2: {
                glm::mat4 m(1.0f);
                glGetUniformfv(fromProgram, loc, &m[0][0]);
                to.setMat4(spec.name, m);
                break;
            }
            case 3: {
                int v = 0;
                glGetUniformiv(fromProgram, loc, &v);
                to.setInt(spec.name, v);
                break;
            }
        }
    }
    // 采样器 unit 编号固定（§14.4 总表 + IBL 6/7/8）——纹理绑定是全局 GL 状态，跨 program 共享
    to.setInt("uAlbedoMap", 0);
    to.setInt("uShadowMap", 1);
    to.setInt("uNormalMap", 2);
    to.setInt("uMetallicMap", 3);
    to.setInt("uRoughnessMap", 4);
    to.setInt("uEmissionMap", 5);
    to.setInt("uIrrMap", 6);
    to.setInt("uPrefMap", 7);
    to.setInt("uBrdfLut", 8);
}

}  // namespace

std::shared_ptr<Mesh> Mesh::create(const MeshData& data) {
    // ===== V1.0 波 1 骨架日（§26.0 冻结项 2）：GL 就绪守卫 =====
    // 无 GL 上下文（NovaTests 等无窗口进程）→ nullptr + warn，防 glGen* 空指针崩溃；
    // 运行期必先 Renderer::init（编辑器/Player 启动路径），行为零变化；
    // 历史遗留 PlayCycleTest 无 GL 崩溃（KNOWN-ISSUES 2026-11-18 段）就此闭环
    if (!Renderer::isInitialized()) {
        Logger::warn("[Mesh] GL 未初始化，跳过网格上传：%s", data.name.c_str());
        return nullptr;
    }
    // 蒙皮网格（V0.5-9）：skinnedVertices 非空 → 蒙皮布局路径（§25.0 冻结：空则旧路径零变化）
    const bool skinned = !data.skinnedVertices.empty();
    if ((!skinned && data.vertices.empty()) || data.indices.empty()) {
        Logger::error("[Mesh] 创建失败：顶点或索引为空（{}）",
                      data.name.empty() ? std::string("未命名") : data.name);
        return nullptr;
    }

    auto mesh = std::shared_ptr<Mesh>(new Mesh());

    glGenVertexArrays(1, &mesh->m_vao);
    glBindVertexArray(mesh->m_vao);

    if (skinned) {
        // 蒙皮交错布局：0=position / 1=normal / 2=uv / 3=tangent / 4=boneIds ivec4 / 5=boneWeights vec4
        //（与 blinn_phong_skinned.vert 一致；SkinnedVertex 前四字段与 Vertex 同序同型）
        constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(SkinnedVertex));

        glGenBuffers(1, &mesh->m_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(data.skinnedVertices.size() * sizeof(SkinnedVertex)),
                     data.skinnedVertices.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &mesh->m_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
                     data.indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(SkinnedVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(SkinnedVertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(SkinnedVertex, uv)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(SkinnedVertex, tangent)));
        glEnableVertexAttribArray(4);
        glVertexAttribIPointer(4, 4, GL_INT, kStride,
                               reinterpret_cast<const void*>(offsetof(SkinnedVertex, boneIds)));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(offsetof(SkinnedVertex, boneWeights)));

        skinnedStates()[mesh.get()] = SkinnedState{};   // 布局标记（isSkinned 判定）
    } else {
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
    }

    mesh->m_indexCount = static_cast<uint32_t>(data.indices.size());
    mesh->m_aabb = data.aabb;

    glBindVertexArray(0);
    return mesh;
}

Mesh::~Mesh() {
    const auto it = skinnedStates().find(this);
    if (it != skinnedStates().end()) {
        if (it->second.boneUbo) glDeleteBuffers(1, &it->second.boneUbo);
        skinnedStates().erase(it);
    }
    if (m_instancedVao) glDeleteVertexArrays(1, &m_instancedVao);
    if (m_instancedVbo) glDeleteBuffers(1, &m_instancedVbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void Mesh::draw() const {
    if (!m_vao || m_indexCount == 0) return;
    glBindVertexArray(m_vao);

    // ===== V0.5-9 蒙皮绘制 =====
    // 仅在当前 pass 为 blinn_phong 系（含 uIrrMap）且非蒙皮 program（无 aBoneWeights 属性）时
    // 换蒙皮 program 并复制逐项 uniform；阴影/描边第二遍/网格等旁路 pass 保持原 program
    //（v1 阴影 = 绑定姿势蒙皮，§25.7 合波裁定 A 已知限制）。
    // 绘制后必须恢复原 program——endFrame 逐项循环的 setBlinnPhongUniforms 经
    // glUniform* 作用于当前绑定 program，不恢复则后续项全部误用蒙皮 program。
    GLuint restoreProgram = 0;
    if (isSkinned()) {
        GLint current = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        std::shared_ptr<Shader> skinnedProg = skinnedShader();
        if (current > 0 && skinnedProg &&
            glGetUniformLocation(static_cast<GLuint>(current), "uIrrMap") >= 0 &&
            glGetAttribLocation(static_cast<GLuint>(current), "aBoneWeights") < 0) {
            restoreProgram = static_cast<GLuint>(current);
            copyUniformsToSkinned(static_cast<GLuint>(current), *skinnedProg);
            skinnedProg->bind();
        }
        // 骨骼 UBO（binding 1）：未上传过（无 Animator/静止）→ 恒等矩阵 = 绑定姿势；
        // 每帧重绑防其它模块占用 binding 1（绑定点是全局 GL 状态）
        SkinnedState& state = skinnedStates()[this];
        if (state.boneUbo == 0) {
            glGenBuffers(1, &state.boneUbo);
            std::vector<glm::mat4> identity(kMaxBones, glm::mat4(1.0f));
            glBindBuffer(GL_UNIFORM_BUFFER, state.boneUbo);
            glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(identity.size() * sizeof(glm::mat4)),
                         identity.data(), GL_STATIC_DRAW);
        }
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, state.boneUbo);
    }

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
    if (restoreProgram != 0) glUseProgram(restoreProgram);   // 恢复调用方 program（见上注释）
    glBindVertexArray(0);
}

void Mesh::drawInstanced(const std::vector<glm::mat4>& models) const {
    if (models.empty() || !m_vao || m_indexCount == 0) return;   // 空向量 no-op

    // V0.5-9：蒙皮网格不实例化（每实例自建 Mesh 指针唯一，分组天然不命中；此处为
    // 手工误调 drawInstanced 的防御回退——按当前骨骼姿势单次绘制）
    if (isSkinned()) {
        draw();
        return;
    }

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

// ===== V0.5-9 蒙皮（§25.7 冻结签名，实现归 c7）=====

void Mesh::setBoneMatrices(const std::vector<glm::mat4>& mats) {
    if (!isSkinned()) return;
    SkinnedState& state = skinnedStates()[this];
    if (mats.empty()) {
        // 空 = 不绑定（§25.7 冻结语义；shader 侧读取未绑定 UBO 为未定义——
        // 防御：绑回恒等 UBO，下一帧 draw 亦会兜底重绑）
        if (state.boneUbo == 0) return;
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0);
        return;
    }
    // 懒建 UBO（绑定 1，上限 64；渲染帧内调用——Animator::onUpdate 在渲染前）
    if (state.boneUbo == 0) glGenBuffers(1, &state.boneUbo);
    const size_t count = std::min(mats.size(), kMaxBones);
    glBindBuffer(GL_UNIFORM_BUFFER, state.boneUbo);
    glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(count * sizeof(glm::mat4)),
                 mats.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, state.boneUbo);
}

bool Mesh::isSkinned() const {
    return skinnedStates().contains(this);
}

}  // namespace NovaEngine
