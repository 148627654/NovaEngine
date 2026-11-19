// TerrainRenderer.cpp —— 地形渲染实现（V0.5-8，c6，§25.6）
//
// 骨架日钩子（SceneRenderer 遍历后、阴影 pass 前调用）：遍历场景全部 Terrain →
// 懒建 GPU 网格（平面网格 + R16 高度纹理，位移在 terrain.vert 顶点着色器内完成）→
// 立即绘制（opaque：深度缓冲保证与 opaque 队列的正确遮挡，与 ParticleRenderer 同
// 钩子点姿势）→ castShadows 时把 CPU 位移网格（与顶点着色器逐样本一致）push 进
// shadowItems（ShadowItem.mesh 复用 Mesh::create 交错布局，shadow.vert 直接可用）。
//
// GPU 资源懒初始化：init/shutdown 当前无人接线（编辑器/Player 仅渲染帧内经钩子调用，
// 此时 GL 上下文存活），与 ParticleRenderer 同一姿势；s_cache 以 Terrain* 为键——
// 组件析构后条目不再被访问（collectAndSubmit 只遍历存活场景），GPU 句柄在 shutdown
// 统一释放（编辑期反复增删地形存在少量 GL 句柄滞留，v1 接受，见最终报告）。

#include <NovaEngine/Terrain/TerrainRenderer.h>

#include <NovaEngine/Terrain/Terrain.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Renderer.h>
#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Render/Texture.h>
#include <NovaEngine/Scene/DirectionalLight.h>
#include <NovaEngine/Scene/GameObject.h>
#include <NovaEngine/Scene/Scene.h>
#include <NovaEngine/Scene/Transform.h>

#include <stb_image.h>   // splat 图解码（实现点 = TextureLoader.cpp）

#include <glad/gl.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace NovaEngine {

namespace {

// ===== 懒初始化全局资源 =====
std::shared_ptr<Shader> s_shader;
bool s_shaderFailedLogged = false;

// 每地形 GPU 缓存（键 = Terrain*；唯一实例、无悬垂访问，见文件头注释）
struct TerrainGpu {
    uint32_t vao = 0, vbo = 0, ebo = 0;         // 平面网格（y=0；位移在顶点着色器）
    uint32_t indexCount = 0;
    uint32_t heightTex = 0;                     // R16 高度图（NEAREST + CLAMP）
    uint32_t splatTex = 0;                      // RGBA8 权重图
    std::shared_ptr<Mesh> shadowMesh;           // CPU 位移网格（shadowItems 用）
    uint32_t res = 0;
    uint32_t heightVersion = ~0u;               // Terrain::m_heightVersion（变化 → 重传）
    glm::vec2 worldSize{0.0f};
    float heightScale = 0.0f;
    float minH = 0.0f, maxH = 0.0f;             // 归一化高度极值（剔除 AABB）
    std::string heightmapPath, splatmapPath;
    std::array<std::string, 4> albedoPath{}, normalPath{};
    std::array<std::shared_ptr<Texture>, 4> albedoTex, normalTex;
};

std::unordered_map<const Terrain*, TerrainGpu> s_cache;

void ensureShader() {
    if (s_shader || s_shaderFailedLogged)
        return;
    s_shader = Shader::createFromFiles("assets/shaders/terrain.vert",
                                       "assets/shaders/terrain.frag");
    if (!s_shader) {
        s_shaderFailedLogged = true;
        Logger::error("[TerrainRenderer] 地形着色器加载失败，地形不绘制（不崩溃）");
        return;
    }
}

// 默认 splat（1×1 全 R 白 = 全权重第 0 层）；懒建
void ensureDefaultSplat(TerrainGpu& g) {
    if (g.splatTex != 0)
        return;
    const uint8_t white[4] = {255, 0, 0, 255};
    glGenTextures(1, &g.splatTex);
    glBindTexture(GL_TEXTURE_2D, g.splatTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// splat RGBA8 上传（空路径/失败 → 默认全 R）
void uploadSplat(TerrainGpu& g, const std::string& path) {
    g.splatmapPath = path;
    ensureDefaultSplat(g);
    if (path.empty())
        return;
    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(path, bytes))
        return;   // 失败保持默认（warn 一次）
    int w = 0, h = 0, ch = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                          &w, &h, &ch, 4);
    if (!data) {
        Logger::warn("[TerrainRenderer] splat 图解码失败: {}（用默认全权重第 0 层）", path);
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g.splatTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
}

// 平面网格 VAO/VBO/EBO（position(x,0,z) + uv；res² 顶点，y 由顶点着色器位移）
void ensureGrid(TerrainGpu& g, uint32_t res, const glm::vec2& worldSize) {
    if (g.vao != 0 && g.res == res && g.worldSize == worldSize)
        return;
    if (g.vao != 0) {
        glDeleteVertexArrays(1, &g.vao);
        glDeleteBuffers(1, &g.vbo);
        glDeleteBuffers(1, &g.ebo);
    }
    g.res = res;
    g.worldSize = worldSize;

    const int n = static_cast<int>(res);
    const float hx = worldSize.x * 0.5f, hz = worldSize.y * 0.5f;
    const float sx = worldSize.x / static_cast<float>(n - 1);
    const float sz = worldSize.y / static_cast<float>(n - 1);
    std::vector<float> verts;   // 交错：x, y(=0), z, u, v
    verts.reserve(static_cast<size_t>(n) * n * 5);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            verts.push_back(-hx + x * sx);
            verts.push_back(0.0f);
            verts.push_back(-hz + z * sz);
            verts.push_back(static_cast<float>(x) / (n - 1));
            verts.push_back(static_cast<float>(z) / (n - 1));
        }
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6);
    for (int z = 0; z < n - 1; ++z)
        for (int x = 0; x < n - 1; ++x) {
            // CCW 正面朝 +Y（默认背面剔除下从上方可见）
            const uint32_t a = static_cast<uint32_t>(z * n + x);
            const uint32_t b = static_cast<uint32_t>(z * n + x + 1);
            const uint32_t c = static_cast<uint32_t>((z + 1) * n + x);
            const uint32_t d = static_cast<uint32_t>((z + 1) * n + x + 1);
            indices.insert(indices.end(), {a, c, b, d, b, c});
        }

    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &g.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);
    constexpr GLsizei kStride = 5 * static_cast<GLsizei>(sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kStride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glBindVertexArray(0);
    g.indexCount = static_cast<uint32_t>(indices.size());
}

// R16 高度纹理（全量上传；NEAREST + CLAMP——与 CPU 阴影网格/物理逐样本一致）。
// 注：本文件匿名命名空间函数无 Terrain 私有访问权限（friend 仅覆盖 TerrainRenderer
// 成员函数），数据一律经参数传入。
void uploadHeightTexture(const std::vector<uint16_t>& heights, uint32_t res, TerrainGpu& g) {
    if (g.heightTex == 0) {
        glGenTextures(1, &g.heightTex);
        glBindTexture(GL_TEXTURE_2D, g.heightTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, static_cast<GLsizei>(res),
                     static_cast<GLsizei>(res), 0, GL_RED, GL_UNSIGNED_SHORT, heights.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    } else {
        glBindTexture(GL_TEXTURE_2D, g.heightTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(res),
                        static_cast<GLsizei>(res), GL_RED, GL_UNSIGNED_SHORT, heights.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// CPU 位移阴影网格：与 terrain.vert 的位移逐样本一致（NEAREST 无滤波差）。
// Mesh::create 走标准交错布局（shadow.vert 直接复用 VAO）。
std::shared_ptr<Mesh> buildShadowMesh(const std::vector<uint16_t>& heights,
                                      const std::vector<glm::vec3>& normals, uint32_t res,
                                      const glm::vec2& worldSize, float heightScale,
                                      float& outMinH, float& outMaxH) {
    const int n = static_cast<int>(res);
    const float hx = worldSize.x * 0.5f, hz = worldSize.y * 0.5f;
    const float sx = worldSize.x / static_cast<float>(n - 1);
    const float sz = worldSize.y / static_cast<float>(n - 1);
    MeshData data;
    data.name = "TerrainShadow";
    data.vertices.reserve(static_cast<size_t>(n) * n);
    outMinH = 1.0f;
    outMaxH = 0.0f;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const size_t idx = static_cast<size_t>(z) * n + x;
            const float h = static_cast<float>(heights[idx]) / 65535.0f;
            outMinH = std::min(outMinH, h);
            outMaxH = std::max(outMaxH, h);
            Vertex v;
            v.position = glm::vec3(-hx + x * sx, h * heightScale, -hz + z * sz);
            v.normal = normals[idx];
            v.uv = glm::vec2(static_cast<float>(x) / (n - 1), static_cast<float>(z) / (n - 1));
            v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            data.vertices.push_back(v);
        }
    }
    data.indices.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6);
    for (int z = 0; z < n - 1; ++z)
        for (int x = 0; x < n - 1; ++x) {
            const uint32_t a = static_cast<uint32_t>(z * n + x);
            const uint32_t b = static_cast<uint32_t>(z * n + x + 1);
            const uint32_t c = static_cast<uint32_t>((z + 1) * n + x);
            const uint32_t d = static_cast<uint32_t>((z + 1) * n + x + 1);
            data.indices.insert(data.indices.end(), {a, c, b, d, b, c});
        }
    for (const Vertex& v : data.vertices) {
        data.aabb.min = glm::min(data.aabb.min, v.position);
        data.aabb.max = glm::max(data.aabb.max, v.position);
    }
    return Mesh::create(data);
}

// 层贴图解析：强引用缓存（AssetDatabase 仅弱缓存——不持强引用会帧间析构 GL 纹理，
// 见 ParticleRenderer 同款注释）；路径变化才重取
void ensureLayerTextures(const Terrain& t, TerrainGpu& g) {
    for (int i = 0; i < 4; ++i) {
        const std::string& ap = t.layers[i].albedoPath;
        if (ap != g.albedoPath[i]) {
            g.albedoPath[i] = ap;
            g.albedoTex[i] = ap.empty() ? nullptr
                                        : AssetDatabase::instance().getTexture(ap, /*srgb=*/true);
            if (!ap.empty() && !g.albedoTex[i])
                Logger::warn("[TerrainRenderer] 层 {} albedo 加载失败: {}（用白色）", i, ap);
        }
        const std::string& np = t.layers[i].normalPath;
        if (np != g.normalPath[i]) {
            g.normalPath[i] = np;
            g.normalTex[i] = np.empty() ? nullptr
                                        : AssetDatabase::instance().getTexture(np, /*srgb=*/false);
            if (!np.empty() && !g.normalTex[i])
                Logger::warn("[TerrainRenderer] 层 {} normal 加载失败: {}（无法线贴图）", i, np);
        }
    }
}

// 世界 AABB（局部 AABB 8 角点经世界矩阵变换；与 SceneRenderer::transformAabb 同方案）
AABB transformAabb(const AABB& local, const glm::mat4& world) {
    AABB out;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? local.max.x : local.min.x,
                               (i & 2) ? local.max.y : local.min.y,
                               (i & 4) ? local.max.z : local.min.z);
        const glm::vec3 p = glm::vec3(world * glm::vec4(corner, 1.0f));
        out.min = glm::min(out.min, p);
        out.max = glm::max(out.max, p);
    }
    return out;
}

}  // namespace

void TerrainRenderer::init() {
    // 显式接线入口（当前无人调用；collectAndSubmit 内部懒初始化兜底，与 ParticleRenderer 同姿势）
    ensureShader();
}

void TerrainRenderer::shutdown() {
    for (auto& [terrain, g] : s_cache) {
        (void)terrain;
        if (g.vao) glDeleteVertexArrays(1, &g.vao);
        if (g.vbo) glDeleteBuffers(1, &g.vbo);
        if (g.ebo) glDeleteBuffers(1, &g.ebo);
        if (g.heightTex) glDeleteTextures(1, &g.heightTex);
        if (g.splatTex) glDeleteTextures(1, &g.splatTex);
        g.shadowMesh.reset();
    }
    s_cache.clear();
    s_shader.reset();
    s_shaderFailedLogged = false;
}

void TerrainRenderer::collectAndSubmit(const Scene& scene, const RenderCamera& camera,
                                       const Frustum& frustum,
                                       std::vector<ShadowItem>& shadowItems) {
    (void)camera;
    ensureShader();
    if (!s_shader)
        return;

    // 灯光（与 SceneRenderer 同逻辑：第一个 DirectionalLight；uLightColor = color × intensity）
    const DirectionalLight* dirLight = nullptr;
    scene.forEachObject([&](GameObject* go) {
        if (!dirLight)
            dirLight = go->getComponent<DirectionalLight>();
    });
    DirectionalLightData light;
    if (dirLight && dirLight->transform()) {
        light.direction = dirLight->transform()->forward();
        light.color = dirLight->color;
        light.intensity = dirLight->intensity;
    }
    light.ambient = scene.ambientColor;
    const bool shadowEnabled = dirLight && dirLight->castShadows;

    scene.forEachObject([&](GameObject* go) {
        if (!go->activeInHierarchy())
            return;
        Terrain* t = go->getComponent<Terrain>();
        if (!t)
            return;
        const Transform* tr = go->transform();
        if (!tr)
            return;

        // 高度图路径变化（Inspector 改路径）→ self-healing 重载 CPU 高度。
        // 首帧（g.res==0）跳过重载：构造/反序列化已加载（避免同帧重复读盘）
        TerrainGpu& g = s_cache[t];
        if (g.res == 0) {
            g.heightmapPath = t->heightmapPath;
        } else if (t->heightmapPath != g.heightmapPath) {
            g.heightmapPath = t->heightmapPath;
            t->loadHeightmap();   // 失败 warn 保持平面（不崩溃）
        }

        // 懒建/重建 GPU 网格（分辨率/尺寸变化时内部重建）
        ensureGrid(g, t->m_res, t->worldSize);
        if (g.heightVersion != t->m_heightVersion) {
            // 首帧/笔刷帧末重算后：高度纹理全量重传 + 阴影网格重建 + 极值刷新
            uploadHeightTexture(t->m_heights, t->m_res, g);
            g.shadowMesh = buildShadowMesh(t->m_heights, t->m_normals, t->m_res, t->worldSize,
                                           t->heightScale, g.minH, g.maxH);
            g.heightVersion = t->m_heightVersion;
        }
        if (g.splatTex == 0 || t->splatmapPath != g.splatmapPath)
            uploadSplat(g, t->splatmapPath);
        ensureLayerTextures(*t, g);

        // 视锥剔除（世界 AABB；被剔除地形不绘制也不进阴影项——与 SceneRenderer
        // 网格项同源语义：被剔除物体不写阴影贴图）
        const glm::mat4 world = tr->worldMatrix();
        AABB localBox;
        localBox.min = glm::vec3(-t->worldSize.x * 0.5f, g.minH * t->heightScale,
                                 -t->worldSize.y * 0.5f);
        localBox.max = glm::vec3(t->worldSize.x * 0.5f, g.maxH * t->heightScale,
                                 t->worldSize.y * 0.5f);
        const AABB worldBox = transformAabb(localBox, world);
        const bool visible = !Renderer::frustumCullingEnabled() || frustum.intersects(worldBox);
        if (!visible)
            return;

        // 单输出 pass（terrain.frag 仅 fragColor）：切 A0，防 MRT FBO 下唯一输出广播
        // 污染法线/线性深度附件（V0.5-5 SSAO，c3 探针约定）
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        s_shader->bind();
        s_shader->setMat4("uModel", world);
        s_shader->setVec3("uLightDir", light.direction);
        s_shader->setVec3("uLightColor", light.color * light.intensity);
        s_shader->setVec3("uAmbient", light.ambient);
        s_shader->setVec4("uTerrainSize",
                          glm::vec4(t->worldSize.x, t->worldSize.y, 0.0f, 0.0f));
        s_shader->setFloat("uHeightScale", t->heightScale);
        s_shader->setInt("uHeightRes", static_cast<int>(t->m_res));
        s_shader->setFloat("uRoughness", 0.9f);
        s_shader->setFloat("uMetallic", 0.0f);

        // 贴图绑定（unit 约定见 terrain.frag 头注释；albedo[1] 与 shadow unit 1 共址——
        // 本波 uShadowEnabled 恒 0，shadow 采样不执行，无冲突）
        for (int i = 0; i < 4; ++i) {
            s_shader->setInt("uUseAlbedoMap[" + std::to_string(i) + "]",
                             g.albedoTex[i] ? 1 : 0);
            if (g.albedoTex[i])
                g.albedoTex[i]->bind(static_cast<uint32_t>(i));
            s_shader->setInt("uAlbedoMap[" + std::to_string(i) + "]", i);
            s_shader->setInt("uUseNormalMap[" + std::to_string(i) + "]",
                             g.normalTex[i] ? 1 : 0);
            if (g.normalTex[i])
                g.normalTex[i]->bind(static_cast<uint32_t>(4 + i));
            s_shader->setInt("uNormalMap[" + std::to_string(i) + "]", 4 + i);
        }
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, g.splatTex);
        s_shader->setInt("uSplatMap", 8);
        glActiveTexture(GL_TEXTURE9);
        glBindTexture(GL_TEXTURE_2D, g.heightTex);
        s_shader->setInt("uHeightMap", 9);
        glActiveTexture(GL_TEXTURE0);
        for (int i = 0; i < 4; ++i)
            s_shader->setFloat("uTileSize[" + std::to_string(i) + "]",
                               std::max(t->layers[i].tileSize, 1e-3f));

        // ===== 阴影（§25.6「阴影采样同 unit 约定」）：unit 1 + blinn_phong 同款 uniform。
        // 本波 C++ 恒关（接缝，见最终报告）：钩子点在 drawShadowPass 之前，Renderer 的
        // 阴影状态/深度纹理无公开访问器（Renderer.h 非 c6 独占文件）；地形只 cast（进
        // shadowItems），不 receive。合波日 c0 补访问器 + 移绘制点到 shadow pass 后即可接通。
        s_shader->setInt("uShadowMap", 1);
        s_shader->setInt("uShadowEnabled", 0);

        // ===== IBL（unit 6/7/8 由 Renderer::setEnvironment 在天空段绑定）：强度按
        // SceneRenderer 同条件推导（cubemap 天空且路径非空）；env 加载失败时 cubemap
        // 未绑定 → 采样返回黑 → 贡献 0（与 blinn_phong 的 setEnvironment(0,0,0,0) 等价）
        s_shader->setFloat(
            "uIblIntensity",
            (scene.skyMode == SkyMode::Cubemap && !scene.skyCubemapPath.empty())
                ? scene.iblIntensity
                : 0.0f);
        s_shader->setFloat("uIblSkyRotation", glm::radians(scene.skyRotation));
        s_shader->setInt("uIrrMap", 6);
        s_shader->setInt("uPrefMap", 7);
        s_shader->setInt("uBrdfLut", 8);

        // 输出 gamma（与 setBlinnPhongUniforms 同款：HDR 0 直通 / 旧路径 1/2.2）
        s_shader->setFloat("uGamma", Renderer::linearOutput() ? 0.0f : 1.0f / 2.2f);

        glBindVertexArray(g.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(g.indexCount), GL_UNSIGNED_INT,
                       nullptr);
        glBindVertexArray(0);

        // 冒烟诊断（c6 验收用）：首次地形绘制后查一次 GL error（后续帧不查防刷屏）
        static bool s_glErrorChecked = false;
        if (!s_glErrorChecked) {
            s_glErrorChecked = true;
            const GLenum err = glGetError();
            if (err != GL_NO_ERROR)
                Logger::warn("[TerrainRenderer] 首帧地形绘制后 GL error: 0x{:04X}", err);
        }

        // castShadows 时地形进 shadowItems（CPU 位移网格与顶点着色器位移逐样本一致）
        if (shadowEnabled && g.shadowMesh)
            shadowItems.push_back(ShadowItem{g.shadowMesh, world});
    });
}

}  // namespace NovaEngine
