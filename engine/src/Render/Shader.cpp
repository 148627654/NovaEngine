#include <NovaEngine/Render/Shader.h>
#include <NovaEngine/Core/Logger.h>

#include <glad/gl.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

namespace NovaEngine {

namespace {

// 读取着色器源码（std::ifstream 的 path 重载：中文路径安全）
bool readFile(const std::filesystem::path& path, std::string& outSource) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    outSource = ss.str();
    return true;
}

// 编译单个着色器阶段；失败经 Logger::error 输出 info log 并返回 0
uint32_t compileStage(GLenum type, const std::string& source, const std::filesystem::path& path) {
    const uint32_t shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<size_t>(std::max(logLen, 1) - 1), '\0');
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        Logger::error("[Shader] 编译失败 {}:\n{}", path.string(), log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// uniform 位置缓存：同一名字只查一次 glGetUniformLocation
int32_t cachedLocation(std::unordered_map<std::string, int32_t>& cache, uint32_t program,
                       const std::string& name) {
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    const int32_t loc = glGetUniformLocation(program, name.c_str());
    cache.emplace(name, loc);
    return loc;
}

}  // namespace

std::shared_ptr<Shader> Shader::createFromFiles(const std::filesystem::path& vsPath,
                                                const std::filesystem::path& fsPath) {
    std::string vsSrc, fsSrc;
    if (!readFile(vsPath, vsSrc)) {
        Logger::error("[Shader] 无法读取顶点着色器: {}", vsPath.string());
        return nullptr;
    }
    if (!readFile(fsPath, fsSrc)) {
        Logger::error("[Shader] 无法读取片元着色器: {}", fsPath.string());
        return nullptr;
    }

    const uint32_t vs = compileStage(GL_VERTEX_SHADER, vsSrc, vsPath);
    const uint32_t fs = compileStage(GL_FRAGMENT_SHADER, fsSrc, fsPath);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return nullptr;
    }

    const uint32_t program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // 链接完成后即可释放着色器阶段对象
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<size_t>(std::max(logLen, 1) - 1), '\0');
        glGetProgramInfoLog(program, logLen, nullptr, log.data());
        Logger::error("[Shader] 链接失败（{} + {}）:\n{}", vsPath.string(), fsPath.string(), log);
        glDeleteProgram(program);
        return nullptr;
    }

    auto shader = std::shared_ptr<Shader>(new Shader());
    shader->m_program = program;
    return shader;
}

Shader::~Shader() {
    if (m_program) glDeleteProgram(m_program);
}

void Shader::bind() const {
    glUseProgram(m_program);
}

void Shader::setInt(const std::string& name, int v) {
    const int32_t loc = cachedLocation(m_uniformCache, m_program, name);
    if (loc >= 0) glUniform1i(loc, v);
}

void Shader::setFloat(const std::string& name, float v) {
    const int32_t loc = cachedLocation(m_uniformCache, m_program, name);
    if (loc >= 0) glUniform1f(loc, v);
}

void Shader::setVec3(const std::string& name, const glm::vec3& v) {
    const int32_t loc = cachedLocation(m_uniformCache, m_program, name);
    if (loc >= 0) glUniform3fv(loc, 1, glm::value_ptr(v));
}

void Shader::setVec4(const std::string& name, const glm::vec4& v) {
    const int32_t loc = cachedLocation(m_uniformCache, m_program, name);
    if (loc >= 0) glUniform4fv(loc, 1, glm::value_ptr(v));
}

void Shader::setMat4(const std::string& name, const glm::mat4& v) {
    const int32_t loc = cachedLocation(m_uniformCache, m_program, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(v));
}

uint32_t Shader::handle() const { return m_program; }

}  // namespace NovaEngine
