#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>

namespace NovaEngine {

// IBL 环境（卷积产物）：irradiance 32² + prefiltered 128² mip 链（5 级）+ BRDF LUT 512²
class IblEnvironment {
public:
    // IBL 内部构造（与 Cubemap 同约定：公开构造/析构，GPU 纹理生命周期随对象）：
    // irr/pref 为本环境所有（析构释放）；lut 为全局共享纹理（IBL 持有，不随环境释放）
    IblEnvironment(uint32_t irradiance, uint32_t prefilter, uint32_t brdfLut);
    ~IblEnvironment();
    void bind(uint32_t irradianceUnit, uint32_t prefilterUnit, uint32_t brdfLutUnit) const;
    uint32_t irradianceId() const; uint32_t prefilterId() const; uint32_t brdfLutId() const;

private:
    uint32_t m_irradiance = 0;
    uint32_t m_prefilter = 0;
    uint32_t m_brdfLut = 0;
};

class IBL {
public:
    static void init(); static void shutdown();   // 惰性初始化（幂等）；shader 失败 → 全部入口安全 no-op
    static bool available();
    // 烘焙：等距柱状 HDRI 绝对路径 → 环境。fragment 卷积默认；compute（4.5 core 直接可用，
    // glad gl:core=4.5 提供 glDispatchCompute，无扩展名；IBL 模块内自行编译 compute program，
    // 不改 Shader 类）编译/执行失败自动回退 fragment；fragment 也失败 → 无 IBL（不黑屏）。
    static std::shared_ptr<IblEnvironment> bake(const std::filesystem::path& equirectPath);
    // 磁盘缓存：key = 源文件 hash + 参数 hash；assets/.novacache/ibl/（复用 BinaryCache 读写器）
    static std::shared_ptr<IblEnvironment> loadCached(const std::filesystem::path& equirectPath);
    static void saveCache(const std::filesystem::path& equirectPath, const IblEnvironment& env);
    static bool cacheExists(const std::filesystem::path& equirectPath);
    static uint32_t brdfLutTextureId();   // 全局一张（与具体 HDRI 无关；首用生成/缓存）
};

}  // namespace NovaEngine
