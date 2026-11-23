#pragma once
#include <memory>
#include <string>
#include <glm/vec3.hpp>
#include "Texture.h"

namespace NovaEngine {

class MaterialAsset;   // 前向声明（Assets 模块；shared_ptr 不完整类型合法，避免 render→assets 头依赖）
class Shader;          // 前向声明（V1.0-5 Shader Graph；shared_ptr 不完整类型合法）

enum class BlendMode { Opaque, AlphaBlend };   // V0.2-8：渲染队列归属只看 blendMode；未来可加 Additive

struct Material {
    glm::vec3 albedo = glm::vec3(0.8f);
    float roughness = 0.6f;
    float metallic = 0.0f;
    std::shared_ptr<Texture> albedoMap = nullptr;    // nullptr = 纯色（运行时 GPU 纹理）
    std::string albedoMapPath;                       // 贴图资产相对路径（序列化用，可为空）
    float alpha = 1.0f;                              // AlphaBlend 时的混合系数
    BlendMode blendMode = BlendMode::Opaque;

    // ===== V0.3-4 PBR 新增 =====
    std::shared_ptr<Texture> normalMap = nullptr;     std::string normalMapPath;     // 必须按 .novameta 线性加载
    std::shared_ptr<Texture> metallicMap = nullptr;   std::string metallicMapPath;
    std::shared_ptr<Texture> roughnessMap = nullptr;  std::string roughnessMapPath;
    std::shared_ptr<Texture> emissionMap = nullptr;   std::string emissionMapPath;
    glm::vec3 emissionColor = glm::vec3(0.0f);
    float emissionIntensity = 1.0f;

    // ===== V0.3-6 材质库双轨 =====
    std::shared_ptr<MaterialAsset> materialAsset = nullptr;   // 非空 = 引用资产（整块引用，无 per-instance override）
    std::string materialAssetPath;                            // 序列化用（相对项目根）

    // ===== V1.0-5 Shader Graph（§27.4 骨架日布，2026-11-23）=====
    // shaderOverride 非空 = 提交走该自定义 shader（SceneRenderer 独立队列，不实例化分组）；
    // shaderGraphPath 为序列化用（.novashadergraph 相对路径；空 = 默认 shader）。
    // operator== 为 = default：新字段自动纳入实例化分组键（shaderGraphPath 不同 → 不合并；
    // shaderOverride 指针不同 → 天然不合并）
    std::shared_ptr<Shader> shaderOverride = nullptr;
    std::string shaderGraphPath;

    // ===== V0.5-14 实例化分组键：逐字段比较（含贴图指针/资产指针/blendMode）=====
    bool operator==(const Material&) const = default;
};

}  // namespace NovaEngine
