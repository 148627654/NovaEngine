// Physics2D.cpp —— 骨架日桩（V0.5-10）：接口冻结于 §25.8；O(n²) 重叠对检测实现归 c8（独占）。
// step 空实现 = 零影响（Scene::update 固定点调用后无 2D 事件）。
#include <NovaEngine/Physics/Physics2D.h>

namespace NovaEngine {

void Physics2D::step(Scene& scene) {
    (void)scene;
}

}  // namespace NovaEngine
