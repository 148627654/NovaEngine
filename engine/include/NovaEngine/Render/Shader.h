#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace NovaEngine {

class Shader {
public:
    // 失败返回 nullptr 并 Logger::error 编译日志
    static std::shared_ptr<Shader> createFromFiles(const std::filesystem::path& vsPath,
                                                   const std::filesystem::path& fsPath);
    // 从内存源码编译（AssetStream/pak 路径用）；失败同上返回 nullptr
    static std::shared_ptr<Shader> createFromSource(const std::string& vsSource,
                                                    const std::string& fsSource);
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    void bind() const;
    void setInt(const std::string& name, int v);
    void setFloat(const std::string& name, float v);
    void setVec3(const std::string& name, const glm::vec3& v);
    void setVec4(const std::string& name, const glm::vec4& v);
    void setMat4(const std::string& name, const glm::mat4& v);
    uint32_t handle() const;

private:
    Shader() = default;
    uint32_t m_program = 0;
    std::unordered_map<std::string, int32_t> m_uniformCache;  // uniform 位置缓存
};

}  // namespace NovaEngine
