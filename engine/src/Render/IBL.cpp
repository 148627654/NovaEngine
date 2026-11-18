// IBL.cpp —— 骨架日桩（V0.5-4）：接口冻结于 §25.2；卷积/缓存实现归 c2（本文件为 c2 独占）。
// 全部入口安全 no-op：available()=false、loadCached/bake 返回 nullptr → SceneRenderer 走
// setEnvironment(0,0,0,0)（blinn_phong.frag 间接光贡献 0，与 2a 前逐像素一致，不黑屏）。
#include <NovaEngine/Render/IBL.h>

namespace NovaEngine {

void IblEnvironment::bind(uint32_t irradianceUnit, uint32_t prefilterUnit, uint32_t brdfLutUnit) const {
    (void)irradianceUnit; (void)prefilterUnit; (void)brdfLutUnit;
}

uint32_t IblEnvironment::irradianceId() const { return 0; }
uint32_t IblEnvironment::prefilterId() const { return 0; }
uint32_t IblEnvironment::brdfLutId() const { return 0; }

void IBL::init() {}
void IBL::shutdown() {}
bool IBL::available() { return false; }

std::shared_ptr<IblEnvironment> IBL::bake(const std::filesystem::path& equirectPath) {
    (void)equirectPath;
    return nullptr;
}

std::shared_ptr<IblEnvironment> IBL::loadCached(const std::filesystem::path& equirectPath) {
    (void)equirectPath;
    return nullptr;
}

void IBL::saveCache(const std::filesystem::path& equirectPath, const IblEnvironment& env) {
    (void)equirectPath; (void)env;
}

bool IBL::cacheExists(const std::filesystem::path& equirectPath) {
    (void)equirectPath;
    return false;
}

uint32_t IBL::brdfLutTextureId() { return 0; }

}  // namespace NovaEngine
