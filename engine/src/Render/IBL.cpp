// IBL.cpp —— IBL 环境光照（V0.5-4，§25.2）
//
// 烘焙链：等距柱状 HDRI → 环境 cubemap（复用 AssetDatabase 天空盒缓存，同源同纹理）→
// fragment 卷积——辐照度 32²（余弦加权半球采样）+ 镜面预滤波 128² 5 级 mip（GGX 重要性
// 采样，roughness→LOD 0..4）+ 全局 BRDF LUT 512²（与 HDRI 无关，首用生成）。
//
// 磁盘缓存：assets/.novacache/ibl/<key>.novabin，key = 源文件 hash + 参数 hash
//（键函数复用 BinaryCache::makeKey）。文件格式镜像 BinaryCache（V0.3-7）的 .novabin
// 布局（magic "NVBI" + version 1 + 48B Header + 有界读取 + 损坏即删）；BinaryCache 的
// 内部读写器为文件静态不可复用（且其 API 只有单贴图类型，装不下 cubemap mip 链），
// 故此处按同格式本地镜像，payloadType 4 = IblEnvironment（辐照度 6 面 + 预滤波 mip
// 链 RGB f32）、5 = IblBrdfLut（RG f32）。
//
// loadCached 被 SceneRenderer 每帧调用：内存 memo（路径 + 文件时间戳）命中仅一次 stat；
// 失效 → 磁盘缓存 → 未命中才烘焙并写缓存。任一 shader 失败 → available()=false，全部
// 入口安全 no-op（SceneRenderer 回退 setEnvironment(0,0,0,0)，blinn_phong.frag 间接光
// 贡献 0，不黑屏）。compute 卷积为契约可选优化，本波未实现（fragment 为默认交付，
// 理由见交付报告）。

#include <NovaEngine/Render/IBL.h>
#include <NovaEngine/Assets/AssetDatabase.h>
#include <NovaEngine/Assets/AssetStream.h>
#include <NovaEngine/Assets/BinaryCache.h>
#include <NovaEngine/Core/Logger.h>
#include <NovaEngine/Render/Cubemap.h>
#include <NovaEngine/Render/Primitives.h>
#include <NovaEngine/Render/Shader.h>

#include <glad/gl.h>

#include <glm/gtc/matrix_transform.hpp>   // perspective / lookAt / radians

#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <vector>

namespace NovaEngine {

namespace {

// ---- 卷积参数（变更须同步 kCacheParamText，旧缓存键自然失效）----
constexpr int kIrrSize = 32;    // 辐照度卷积分辨率（²/面）
constexpr int kPrefSize = 128;  // 预滤波基础分辨率（²/面）
constexpr int kPrefMips = 5;    // 预滤波 mip 级数（roughness→LOD 0..4）
constexpr int kLutSize = 512;   // BRDF LUT 分辨率（²）
constexpr const char* kCacheParamText = "ibl_v1|irr32s512|pref128m5s128|brdf512s256";
constexpr const char* kLutParamText = "ibl_brdf_lut_v1|512|s256";

// ---- .novabin 镜像（同 BinaryCache 格式；payloadType 独占 4/5，文件在 ibl/ 子目录）----
constexpr char kMagic[4] = {'N', 'V', 'B', 'I'};
constexpr uint32_t kCacheVersion = 1;
constexpr uint32_t kTypeIblEnv = 4;    // IblEnvironment：irr 6 面 + pref mip 链（RGB f32）
constexpr uint32_t kTypeBrdfLut = 5;   // IblBrdfLut：RG f32
constexpr size_t kHeaderSize = 48;

// 全局状态（惰性初始化；shader 失败 → s_available=false，全部入口安全 no-op）
bool s_initialized = false;
bool s_available = false;
std::shared_ptr<Shader> s_irrShader;    // equirect_to_cube.vert + ibl_irradiance_conv.frag
std::shared_ptr<Shader> s_prefShader;   // equirect_to_cube.vert + ibl_prefilter.frag
std::shared_ptr<Shader> s_brdfShader;   // 内嵌全屏三角形 VS + ibl_brdf_lut.frag
std::shared_ptr<Mesh> s_convCube;       // 单位立方体（6 面渲染共用，Primitives 缓存）
uint32_t s_fsVao = 0;                   // 全屏三角形空 VAO（BRDF LUT 渲染）
uint32_t s_brdfLutId = 0;               // 全局 BRDF LUT（首用生成/磁盘缓存）

// 每帧 loadCached 的内存 memo（key = 归一化绝对路径）
struct MemoEntry {
    std::filesystem::file_time_type mtime{};
    std::shared_ptr<IblEnvironment> env;   // nullptr = 已判定失败（mtime 不变不重试）
};
std::unordered_map<std::string, MemoEntry> s_memo;

// BRDF LUT 顶点段内嵌源码（全屏三角形：空 VAO + gl_VertexID，与 SkyboxRenderer 同模式；
// 片元段为资产文件 assets/shaders/ibl_brdf_lut.frag）
constexpr const char* kFullscreenVs = R"(
#version 450 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// ---- 小端序列化辅助（x86 原生，直接 memcpy；同 BinaryCache）----

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    const size_t o = b.size();
    b.resize(o + 4);
    std::memcpy(b.data() + o, &v, 4);
}
void putU64(std::vector<uint8_t>& b, uint64_t v) {
    const size_t o = b.size();
    b.resize(o + 8);
    std::memcpy(b.data() + o, &v, 8);
}
void putBytes(std::vector<uint8_t>& b, const void* p, size_t n) {
    const size_t o = b.size();
    b.resize(o + n);
    if (n) std::memcpy(b.data() + o, p, n);
}

// 有界读取器：所有字段带越界检查（读损坏文件不越界、不崩溃）
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& d) : m_d(d) {}
    bool u32(uint32_t& out) { return take(4, &out); }
    bool u64(uint64_t& out) { return take(8, &out); }
    bool bytes(void* dst, size_t n) {
        if (m_pos + n > m_d.size()) return false;
        if (n) std::memcpy(dst, m_d.data() + m_pos, n);
        m_pos += n;
        return true;
    }
    size_t remaining() const { return m_d.size() - m_pos; }

private:
    bool take(size_t n, void* out) {
        if (m_pos + n > m_d.size()) return false;
        std::memcpy(out, m_d.data() + m_pos, n);
        m_pos += n;
        return true;
    }
    const std::vector<uint8_t>& m_d;
    size_t m_pos = 0;
};

std::filesystem::path iblCacheDir() {
    // 项目根优先（与 BinaryCache 同锚点）；未设置回退当前目录
    std::filesystem::path root = AssetDatabase::projectRoot();
    if (root.empty()) root = std::filesystem::current_path();
    return root / "assets" / ".novacache" / "ibl";
}

bool ensureIblCacheDir() {
    std::error_code ec;
    std::filesystem::create_directories(iblCacheDir(), ec);
    if (ec) {
        Logger::warn("[IBL] 缓存目录创建失败: {}", iblCacheDir().string());
        return false;
    }
    return true;
}

// 损坏处理：删除该缓存文件 + warn + false（同 BinaryCache「损坏即删即重导」）
bool corrupt(const std::string& key) {
    std::error_code ec;
    std::filesystem::remove(iblCacheDir() / (key + ".novabin"), ec);
    Logger::warn("[IBL] 缓存损坏，已删除并重烘焙: {}", key);
    return false;
}

bool writeCacheFile(const std::string& key, uint32_t payloadType,
                    const std::vector<uint8_t>& payload) {
    if (!ensureIblCacheDir()) return false;
    std::vector<uint8_t> file;
    file.reserve(kHeaderSize + payload.size());
    // Header 48B（小端）
    file.insert(file.end(), kMagic, kMagic + 4);
    putU32(file, kCacheVersion);
    putU32(file, payloadType);
    putU32(file, 0);            // flags
    putU64(file, 0);            // sourceSize：与 BinaryCache 冻结 API 一致保留 0
    putU64(file, payload.size());
    putU64(file, 0);            // reserved
    putU64(file, 0);            // reserved2
    file.insert(file.end(), payload.begin(), payload.end());

    std::ofstream out(iblCacheDir() / (key + ".novabin"), std::ios::binary);
    if (!out || !out.write(reinterpret_cast<const char*>(file.data()),
                           static_cast<std::streamsize>(file.size()))) {
        Logger::warn("[IBL] 缓存写入失败: {}", key);
        return false;
    }
    return true;
}

// 读 + Header 校验（magic/version/payloadType/payloadSize==剩余长度）；未命中静默 false
bool readCacheFile(const std::string& key, uint32_t expectType,
                   std::vector<uint8_t>& outPayload) {
    std::vector<uint8_t> file;
    std::ifstream in(iblCacheDir() / (key + ".novabin"), std::ios::binary);
    if (!in) return false;   // 缓存未命中（正常路径，不打日志）

    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) return false;
    in.seekg(0, std::ios::beg);
    file.resize(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(file.data()), size)) return corrupt(key);

    if (file.size() < kHeaderSize) return corrupt(key);
    if (std::memcmp(file.data(), kMagic, 4) != 0) return corrupt(key);

    Reader r(file);
    uint32_t magicWord = 0, version = 0, payloadType = 0, flags = 0;
    uint64_t sourceSize = 0, payloadSize = 0, reserved = 0, reserved2 = 0;
    if (!r.u32(magicWord) || !r.u32(version) || !r.u32(payloadType) || !r.u32(flags))
        return corrupt(key);
    if (!r.u64(sourceSize) || !r.u64(payloadSize) || !r.u64(reserved) || !r.u64(reserved2))
        return corrupt(key);
    if (version != kCacheVersion) return corrupt(key);
    if (payloadType != expectType) return corrupt(key);
    if (payloadSize != r.remaining()) return corrupt(key);

    outPayload.assign(file.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), file.end());
    return true;
}

// ---- 路径与文件 ----

std::filesystem::path resolveAbsolute(const std::filesystem::path& p) {
    if (p.is_absolute()) return p.lexically_normal();
    std::filesystem::path root = AssetDatabase::projectRoot();
    if (root.empty()) root = std::filesystem::current_path();
    return (root / p).lexically_normal();
}

std::optional<std::filesystem::file_time_type> fileMtime(const std::filesystem::path& p) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return std::nullopt;
    return t;
}

// ---- GL 辅助 ----

// 烘焙穿插在渲染帧内（SceneRenderer 天空段）：进入保存、析构恢复全部触碰状态
struct GlStateGuard {
    GlStateGuard() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
        depthTest = glIsEnabled(GL_DEPTH_TEST);
        cullFace = glIsEnabled(GL_CULL_FACE);
        blend = glIsEnabled(GL_BLEND);
        stencilTest = glIsEnabled(GL_STENCIL_TEST);
        scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    }
    ~GlStateGuard() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao));
        glDepthFunc(static_cast<GLenum>(depthFunc));
        glCullFace(static_cast<GLenum>(cullMode));
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        setEnabled(GL_DEPTH_TEST, depthTest);
        setEnabled(GL_CULL_FACE, cullFace);
        setEnabled(GL_BLEND, blend);
        setEnabled(GL_STENCIL_TEST, stencilTest);
        setEnabled(GL_SCISSOR_TEST, scissorTest);
        glActiveTexture(static_cast<GLenum>(activeTexture));
    }
    static void setEnabled(GLenum cap, GLboolean on) {
        if (on) glEnable(cap);
        else glDisable(cap);
    }
    GLint fbo = 0, program = 0, vao = 0, viewport[4] = {0, 0, 0, 0}, activeTexture = 0;
    GLint depthFunc = 0, cullMode = 0;
    GLfloat clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLboolean depthTest = GL_FALSE, cullFace = GL_FALSE, blend = GL_FALSE;
    GLboolean stencilTest = GL_FALSE, scissorTest = GL_FALSE;
};

// cubemap 分配（RGB16F；mips>1 时显式分配 mip 链并设 MAX_LEVEL 保证完整）
uint32_t allocCubeTexture(int size, int mips) {
    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mips > 1) {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, mips - 1);
    } else {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    for (int m = 0; m < mips; ++m) {
        const int s = size >> m;
        for (int f = 0; f < 6; ++f)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, m, GL_RGB16F, s, s, 0,
                         GL_RGB, GL_FLOAT, nullptr);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return tex;
}

// 浮点像素上传（面顺序 +X,-X,+Y,-Y,+Z,-Z；mip 0..mips-1）
uint32_t uploadCubeTexture(const std::vector<float>& pixels, int size, int mips) {
    uint32_t tex = allocCubeTexture(size, mips);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    size_t off = 0;
    for (int m = 0; m < mips; ++m) {
        const int s = size >> m;
        for (int f = 0; f < 6; ++f) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, m, GL_RGB16F, s, s, 0,
                         GL_RGB, GL_FLOAT, pixels.data() + off);
            off += static_cast<size_t>(s) * s * 3;
        }
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return tex;
}

// 读回（写磁盘缓存用）
std::vector<float> readCubeTexture(uint32_t tex, int size, int mips) {
    size_t total = 0;
    for (int m = 0; m < mips; ++m)
        total += static_cast<size_t>(6) * (size >> m) * (size >> m) * 3;
    std::vector<float> out(total);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    size_t off = 0;
    for (int m = 0; m < mips; ++m) {
        const int s = size >> m;
        for (int f = 0; f < 6; ++f) {
            glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, m, GL_RGB, GL_FLOAT,
                          out.data() + off);
            off += static_cast<size_t>(s) * s * 3;
        }
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return out;
}

// 卷积目标 FBO：颜色附件按 (face, mip) 挂载目标 cubemap；深度 RBO maxSize²
//（深度必需：立方体近面必须遮挡远面，同 Cubemap::createFromEquirect 评审 P1-1）
struct ConvFbo {
    bool init(int maxSize) {
        glGenFramebuffers(1, &fbo);
        glGenRenderbuffers(1, &depthRbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, maxSize, maxSize);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                  depthRbo);
        return fbo != 0 && depthRbo != 0;
    }
    bool complete() const {
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    void attach(uint32_t cubeTex, int face, int mip) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubeTex, mip);
    }
    void release() {
        if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (depthRbo) { glDeleteRenderbuffers(1, &depthRbo); depthRbo = 0; }
    }
    uint32_t fbo = 0;
    uint32_t depthRbo = 0;
};

// 6 面方向/up 组合（GL cubemap face 约定，同 Cubemap::createFromEquirect）
const glm::vec3 kFaceDirs[6] = {
    glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec3( 0.0f,-1.0f, 0.0f),
    glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec3( 0.0f, 0.0f,-1.0f),
};
const glm::vec3 kFaceUps[6] = {
    glm::vec3(0.0f,-1.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f,-1.0f),
    glm::vec3(0.0f,-1.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f),
};

// 渲染单位立方体一面（相机在原点；深度保证近面覆盖远面；视口 = 该 mip 面尺寸）
void drawCubeFace(int face, int size, Shader& shader) {
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), kFaceDirs[face], kFaceUps[face]);
    shader.setMat4("uView", view);
    shader.setMat4("uProj", proj);
    glViewport(0, 0, size, size);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   // 边缘防缝隙：每面先清黑
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    s_convCube->draw();
}

// 辐照度卷积：6 面 32²（余弦加权半球采样，ibl_irradiance_conv.frag）
uint32_t convolveIrradiance(const Cubemap& envMap, int size) {
    uint32_t outTex = allocCubeTexture(size, 1);
    ConvFbo fbo;
    if (!fbo.init(size)) {
        Logger::error("[IBL] 辐照度卷积 FBO 创建失败");
        glDeleteTextures(1, &outTex);
        return 0;
    }

    GlStateGuard guard;
    glDisable(GL_CULL_FACE);   // 相机在立方体中心：可见的是各面背面，剔除会画空
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    s_irrShader->bind();
    s_irrShader->setInt("uEnvironmentMap", 0);
    envMap.bind(0);

    fbo.attach(outTex, 0, 0);
    if (!fbo.complete()) {
        Logger::error("[IBL] 辐照度卷积 FBO 不完整（{}² RGB16F）", size);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        fbo.release();
        glDeleteTextures(1, &outTex);
        return 0;
    }
    for (int f = 0; f < 6; ++f) {
        fbo.attach(outTex, f, 0);
        drawCubeFace(f, size, *s_irrShader);
    }

    // unit 0 解绑防串扰；guard 析构恢复 FBO/视口/程序等
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    fbo.release();
    return outTex;
}

// 镜面预滤波：6 面 × 5 级 mip（GGX 重要性采样，ibl_prefilter.frag；roughness = mip/4）
uint32_t convolvePrefilter(const Cubemap& envMap, int size, int mips, int srcFaceSize) {
    uint32_t outTex = allocCubeTexture(size, mips);
    ConvFbo fbo;
    if (!fbo.init(size)) {
        Logger::error("[IBL] 预滤波 FBO 创建失败");
        glDeleteTextures(1, &outTex);
        return 0;
    }

    GlStateGuard guard;
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    s_prefShader->bind();
    s_prefShader->setInt("uEnvironmentMap", 0);
    s_prefShader->setFloat("uSourceFaceSize", static_cast<float>(srcFaceSize));
    envMap.bind(0);

    fbo.attach(outTex, 0, 0);
    if (!fbo.complete()) {
        Logger::error("[IBL] 预滤波 FBO 不完整（{}² RGB16F×{} mip）", size, mips);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        fbo.release();
        glDeleteTextures(1, &outTex);
        return 0;
    }
    for (int m = 0; m < mips; ++m) {
        const int mipSize = size >> m;
        const float roughness = static_cast<float>(m) / static_cast<float>(mips - 1);
        s_prefShader->setFloat("uRoughness", roughness);
        for (int f = 0; f < 6; ++f) {
            fbo.attach(outTex, f, m);
            drawCubeFace(f, mipSize, *s_prefShader);
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    fbo.release();
    return outTex;
}

// ---- BRDF LUT（全局一张，与 HDRI 无关）----

std::string brdfLutCacheKey() {
    return BinaryCache::makeKey({}, kLutParamText);
}

bool uploadBrdfLutTexture(const std::vector<float>& pixels, int size) {
    if (pixels.size() != static_cast<size_t>(size) * size * 2) return false;
    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, size, size, 0, GL_RG, GL_FLOAT, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    s_brdfLutId = tex;
    return true;
}

bool readBrdfLutCache(const std::string& key, std::vector<float>& outPixels) {
    std::vector<uint8_t> payload;
    if (!readCacheFile(key, kTypeBrdfLut, payload)) return false;

    Reader r(payload);
    uint32_t w = 0, h = 0;
    if (!r.u32(w) || !r.u32(h)) return corrupt(key);
    if (w != static_cast<uint32_t>(kLutSize) || h != static_cast<uint32_t>(kLutSize))
        return corrupt(key);
    const size_t expect = static_cast<size_t>(w) * h * 2 * sizeof(float);
    if (r.remaining() != expect) return corrupt(key);
    outPixels.resize(static_cast<size_t>(w) * h * 2);
    return r.bytes(outPixels.data(), expect);
}

bool writeBrdfLutCache(const std::string& key, const std::vector<float>& pixels, int size) {
    if (pixels.size() != static_cast<size_t>(size) * size * 2) return false;
    std::vector<uint8_t> payload;
    payload.reserve(8 + pixels.size() * sizeof(float));
    putU32(payload, static_cast<uint32_t>(size));
    putU32(payload, static_cast<uint32_t>(size));
    putBytes(payload, pixels.data(), pixels.size() * sizeof(float));
    return writeCacheFile(key, kTypeBrdfLut, payload);
}

// 幂等：已生成/缓存命中直接返回 true
bool ensureBrdfLut() {
    if (s_brdfLutId != 0) return true;

    // 磁盘缓存命中 → 直接上传（全局一张，键只含参数，不含 HDRI）
    const std::string key = brdfLutCacheKey();
    std::vector<float> pixels;
    if (readBrdfLutCache(key, pixels)) {
        if (uploadBrdfLutTexture(pixels, kLutSize)) {
            Logger::info("[IBL] BRDF LUT 缓存命中 {}（{}²）", key, kLutSize);
            return true;
        }
        return false;
    }

    // 渲染：512² RG16F 目标 + 全屏三角形（空 VAO + gl_VertexID）
    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, kLutSize, kLutSize, 0, GL_RG, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    uint32_t fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Logger::error("[IBL] BRDF LUT FBO 创建失败（{}² RG16F）", kLutSize);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        return false;
    }

    GlStateGuard guard;
    if (!s_fsVao) glGenVertexArrays(1, &s_fsVao);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);

    s_brdfShader->bind();
    glViewport(0, 0, kLutSize, kLutSize);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindVertexArray(s_fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 读回写缓存（一次成功即可；失败只影响下次启动速度，不影响本次）
    pixels.resize(static_cast<size_t>(kLutSize) * kLutSize * 2);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_FLOAT, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    writeBrdfLutCache(key, pixels, kLutSize);

    glDeleteFramebuffers(1, &fbo);
    s_brdfLutId = tex;
    Logger::info("[IBL] BRDF LUT 生成完成（{}²×256 样本，已写缓存）", kLutSize);
    return true;
}

// ---- 磁盘缓存（IblEnvironment）----

std::shared_ptr<IblEnvironment> loadEnvFromCache(const std::string& key) {
    std::vector<uint8_t> payload;
    if (!readCacheFile(key, kTypeIblEnv, payload)) return nullptr;

    Reader r(payload);
    uint32_t irrSize = 0, prefSize = 0, prefMips = 0;
    if (!r.u32(irrSize) || !r.u32(prefSize) || !r.u32(prefMips)) {
        corrupt(key);
        return nullptr;
    }
    if (irrSize != static_cast<uint32_t>(kIrrSize) ||
        prefSize != static_cast<uint32_t>(kPrefSize) ||
        prefMips != static_cast<uint32_t>(kPrefMips)) {
        corrupt(key);
        return nullptr;
    }
    size_t prefFloats = 0;
    for (int m = 0; m < kPrefMips; ++m)
        prefFloats += static_cast<size_t>(6) * (kPrefSize >> m) * (kPrefSize >> m) * 3;
    const size_t irrFloats = static_cast<size_t>(6) * kIrrSize * kIrrSize * 3;
    if (r.remaining() != (irrFloats + prefFloats) * sizeof(float)) {
        corrupt(key);
        return nullptr;
    }

    std::vector<float> irr(irrFloats);
    std::vector<float> pref(prefFloats);
    if (!r.bytes(irr.data(), irr.size() * sizeof(float)) ||
        !r.bytes(pref.data(), pref.size() * sizeof(float))) {
        corrupt(key);
        return nullptr;
    }

    const uint32_t irrTex = uploadCubeTexture(irr, kIrrSize, 1);
    const uint32_t prefTex = uploadCubeTexture(pref, kPrefSize, kPrefMips);
    const uint32_t lut = IBL::brdfLutTextureId();
    if (!irrTex || !prefTex || !lut) {
        if (irrTex) glDeleteTextures(1, &irrTex);
        if (prefTex) glDeleteTextures(1, &prefTex);
        Logger::error("[IBL] 缓存纹理上传失败 {}", key);
        return nullptr;
    }
    return std::make_shared<IblEnvironment>(irrTex, prefTex, lut);
}

}  // namespace

// ---- IblEnvironment ----

IblEnvironment::IblEnvironment(uint32_t irradiance, uint32_t prefilter, uint32_t brdfLut)
    : m_irradiance(irradiance), m_prefilter(prefilter), m_brdfLut(brdfLut) {}

IblEnvironment::~IblEnvironment() {
    // BRDF LUT 为全局共享纹理（IBL 持有），本环境只释放自己的两张卷积图
    if (m_irradiance) glDeleteTextures(1, &m_irradiance);
    if (m_prefilter) glDeleteTextures(1, &m_prefilter);
}

void IblEnvironment::bind(uint32_t irradianceUnit, uint32_t prefilterUnit,
                          uint32_t brdfLutUnit) const {
    glActiveTexture(GL_TEXTURE0 + irradianceUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_irradiance);
    glActiveTexture(GL_TEXTURE0 + prefilterUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_prefilter);
    glActiveTexture(GL_TEXTURE0 + brdfLutUnit);
    glBindTexture(GL_TEXTURE_2D, m_brdfLut);
    glActiveTexture(GL_TEXTURE0);   // 恢复常用激活单元
}

uint32_t IblEnvironment::irradianceId() const { return m_irradiance; }
uint32_t IblEnvironment::prefilterId() const { return m_prefilter; }
uint32_t IblEnvironment::brdfLutId() const { return m_brdfLut; }

// ---- IBL ----

void IBL::init() {
    if (s_initialized) return;
    s_initialized = true;

    // 卷积渲染用单位立方体（Primitives 内部缓存）与三个卷积 program；
    // 任一失败 → s_available=false，全部入口安全 no-op
    s_convCube = Primitives::cube();
    s_irrShader = Shader::createFromFiles("assets/shaders/equirect_to_cube.vert",
                                          "assets/shaders/ibl_irradiance_conv.frag");
    s_prefShader = Shader::createFromFiles("assets/shaders/equirect_to_cube.vert",
                                           "assets/shaders/ibl_prefilter.frag");
    // BRDF LUT：片元源为资产文件，顶点段内嵌（全屏三角形，无独立 .vert 资产）
    std::string fsSource;
    if (AssetStream::readAllText("assets/shaders/ibl_brdf_lut.frag", fsSource))
        s_brdfShader = Shader::createFromSource(kFullscreenVs, fsSource);

    s_available = s_convCube && s_irrShader && s_prefShader && s_brdfShader;
    if (!s_available)
        Logger::error("[IBL] 卷积着色器初始化失败：IBL 不可用（回退无环境间接光，不黑屏）");
}

void IBL::shutdown() {
    if (!s_initialized) return;
    s_memo.clear();   // IblEnvironment 析构释放 irr/pref GPU 纹理
    if (s_brdfLutId) {
        glDeleteTextures(1, &s_brdfLutId);
        s_brdfLutId = 0;
    }
    if (s_fsVao) {
        glDeleteVertexArrays(1, &s_fsVao);
        s_fsVao = 0;
    }
    s_convCube.reset();
    s_irrShader.reset();
    s_prefShader.reset();
    s_brdfShader.reset();
    s_available = false;
    s_initialized = false;
}

bool IBL::available() {
    return s_initialized && s_available;
}

uint32_t IBL::brdfLutTextureId() {
    init();
    if (!s_available) return 0;
    ensureBrdfLut();
    return s_brdfLutId;
}

std::shared_ptr<IblEnvironment> IBL::bake(const std::filesystem::path& equirectPath) {
    init();
    if (!s_available) return nullptr;
    const auto t0 = std::chrono::steady_clock::now();

    // 1. 环境 cubemap：优先复用 AssetDatabase 天空盒缓存（同源同纹理，避免重复转换）；
    //    无缓存/独立使用回退 512² 等距柱状转换
    std::shared_ptr<Cubemap> envMap = AssetDatabase::instance().getCubemap(equirectPath);
    if (!envMap)
        envMap = Cubemap::createFromEquirect(resolveAbsolute(equirectPath), 512);
    if (!envMap) {
        Logger::error("[IBL] 环境 cubemap 加载失败 {}：IBL 不可用", equirectPath.string());
        return nullptr;
    }

    // 2. BRDF LUT（全局一张，幂等）
    if (!ensureBrdfLut()) {
        Logger::error("[IBL] BRDF LUT 生成失败：IBL 不可用");
        return nullptr;
    }

    // 3. 辐照度卷积 32²（fragment 路径）
    const uint32_t irr = convolveIrradiance(*envMap, kIrrSize);
    if (!irr) {
        Logger::error("[IBL] 辐照度卷积失败");
        return nullptr;
    }

    // 4. 镜面预滤波 128² 5 级 mip
    const uint32_t pref = convolvePrefilter(*envMap, kPrefSize, kPrefMips, envMap->faceSize());
    if (!pref) {
        Logger::error("[IBL] 预滤波卷积失败");
        glDeleteTextures(1, &irr);
        return nullptr;
    }

    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    Logger::info("[IBL] 烘焙完成 {}：辐照度 {}² + 预滤波 {}²×{} mip + BRDF LUT {}²，"
                 "耗时 {:.1f} ms",
                 equirectPath.string(), kIrrSize, kPrefSize, kPrefMips, kLutSize, ms);
    return std::make_shared<IblEnvironment>(irr, pref, s_brdfLutId);
}

std::shared_ptr<IblEnvironment> IBL::loadCached(const std::filesystem::path& equirectPath) {
    init();
    if (!s_available) return nullptr;

    const std::filesystem::path abs = resolveAbsolute(equirectPath);
    const std::string memoKey = abs.generic_string();

    // 内存 memo：SceneRenderer 每帧调用；命中 = 一次文件时间戳查询，零磁盘/GPU 开销
    if (auto it = s_memo.find(memoKey); it != s_memo.end()) {
        if (const auto m = fileMtime(abs); m && *m == it->second.mtime)
            return it->second.env;   // env 可为 nullptr（已判定失败，mtime 不变不重试）
        s_memo.erase(it);            // 文件变更 → 失效重建
    }

    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(abs, bytes)) {
        // 6 面目录源/文件不存在/读取失败：无 IBL（SceneRenderer 回退，不黑屏）
        if (const auto m = fileMtime(abs)) s_memo[memoKey] = MemoEntry{*m, nullptr};
        return nullptr;
    }
    const std::string cacheKey = BinaryCache::makeKey(bytes, kCacheParamText);

    const auto t0 = std::chrono::steady_clock::now();
    std::shared_ptr<IblEnvironment> env = loadEnvFromCache(cacheKey);
    if (!env) {
        // 缓存未命中/损坏 → 完整烘焙 + 写缓存（烘焙自身有耗时日志）
        env = bake(equirectPath);
        if (env) saveCache(equirectPath, *env);
    } else {
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
        Logger::info("[IBL] 卷积缓存命中 {}（含纹理上传 {:.1f} ms）", cacheKey, ms);
    }

    if (const auto m = fileMtime(abs)) s_memo[memoKey] = MemoEntry{*m, env};
    return env;
}

void IBL::saveCache(const std::filesystem::path& equirectPath, const IblEnvironment& env) {
    if (!s_available) return;   // shader 失败：全部入口安全 no-op

    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(equirectPath, bytes)) {
        Logger::warn("[IBL] 缓存写入失败：源文件不可读 {}", equirectPath.string());
        return;
    }
    const std::string key = BinaryCache::makeKey(bytes, kCacheParamText);

    // GPU 读回（RGB16F → f32；写一次即可，读回开销只在烘焙路径）
    const std::vector<float> irr = readCubeTexture(env.irradianceId(), kIrrSize, 1);
    const std::vector<float> pref = readCubeTexture(env.prefilterId(), kPrefSize, kPrefMips);
    size_t prefExpect = 0;
    for (int m = 0; m < kPrefMips; ++m)
        prefExpect += static_cast<size_t>(6) * (kPrefSize >> m) * (kPrefSize >> m) * 3;
    const size_t irrExpect = static_cast<size_t>(6) * kIrrSize * kIrrSize * 3;
    if (irr.size() != irrExpect || pref.size() != prefExpect) {
        Logger::warn("[IBL] 缓存写入失败：GPU 读回尺寸不符");
        return;
    }

    // 载荷：u32 irrSize | u32 prefSize | u32 prefMips | irr 6 面 f32 | pref mip 链 f32
    std::vector<uint8_t> payload;
    payload.reserve(12 + (irr.size() + pref.size()) * sizeof(float));
    putU32(payload, static_cast<uint32_t>(kIrrSize));
    putU32(payload, static_cast<uint32_t>(kPrefSize));
    putU32(payload, static_cast<uint32_t>(kPrefMips));
    putBytes(payload, irr.data(), irr.size() * sizeof(float));
    putBytes(payload, pref.data(), pref.size() * sizeof(float));
    if (writeCacheFile(key, kTypeIblEnv, payload))
        Logger::info("[IBL] 卷积缓存已写入 assets/.novacache/ibl/{}.novabin", key);
}

bool IBL::cacheExists(const std::filesystem::path& equirectPath) {
    if (!s_available) return false;
    std::vector<uint8_t> bytes;
    if (!AssetStream::readAllBytes(equirectPath, bytes)) return false;
    const std::string key = BinaryCache::makeKey(bytes, kCacheParamText);
    std::error_code ec;
    return std::filesystem::exists(iblCacheDir() / (key + ".novabin"), ec);
}

}  // namespace NovaEngine
