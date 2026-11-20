// TestMath.cpp —— 数学族（§26.1 覆盖清单 4：Frustum AABB 相交）
//
// 对 KNOWN-ISSUES：
//  - V0.5-14 剔除验收项 2：kPlaneEpsilon 剔除安全裕度防边缘物体帧间闪出/闪没（边界用例）
//  - 正交投影同样成立（w 行平面退化为平行面，无需特判）
//
// 纯数学类，无 GL 依赖。坐标系：eye 在 (0,0,5) 朝 -Z 看（GL 裁剪 z∈[-1,1] 同引擎约定）。

#include <catch2/catch_test_macros.hpp>

#include <NovaEngine/Render/Frustum.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace NovaEngine;

namespace {

// 透视 viewProj：fov 45°、near 0.1、far 50
glm::mat4 makePerspectiveViewProj() {
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),   // eye
                                       glm::vec3(0.0f, 0.0f, -5.0f),  // center（朝 -Z）
                                       glm::vec3(0.0f, 1.0f, 0.0f));  // up
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 50.0f);
    return proj * view;
}

glm::mat4 makeOrthoViewProj() {
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
                                       glm::vec3(0.0f, 0.0f, -5.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 50.0f);
    return proj * view;
}

AABB box(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    AABB a;
    a.min = glm::vec3(minX, minY, minZ);
    a.max = glm::vec3(maxX, maxY, maxZ);
    return a;
}

}  // namespace

TEST_CASE("Math: AABB inside perspective frustum intersects", "[math][frustum]") {
    const Frustum f = Frustum::fromViewProj(makePerspectiveViewProj());
    REQUIRE(f.intersects(box(-0.5f, -0.5f, -1.0f, 0.5f, 0.5f, 1.0f)));   // 视锥中心
    REQUIRE(f.intersects(box(0.0f, 0.0f, -0.2f, 0.1f, 0.1f, -0.1f)));    // 贴近 near 处
    REQUIRE(f.intersects(box(-100.0f, -100.0f, -100.0f, 100.0f, 100.0f, 100.0f)));  // 包裹视锥
}

TEST_CASE("Math: AABB outside perspective frustum culled", "[math][frustum]") {
    const Frustum f = Frustum::fromViewProj(makePerspectiveViewProj());
    REQUIRE_FALSE(f.intersects(box(-0.5f, -0.5f, 6.0f, 0.5f, 0.5f, 7.0f)));    // 相机背后（near 之前）
    REQUIRE_FALSE(f.intersects(box(-0.5f, -0.5f, -100.0f, 0.5f, 0.5f, -90.0f)));  // 远于 far
    REQUIRE_FALSE(f.intersects(box(50.0f, -0.5f, -5.0f, 60.0f, 0.5f, -4.0f)));    // 右外侧
    REQUIRE_FALSE(f.intersects(box(-60.0f, -0.5f, -5.0f, -50.0f, 0.5f, -4.0f)));  // 左外侧
    REQUIRE_FALSE(f.intersects(box(-0.5f, 50.0f, -5.0f, 0.5f, 60.0f, -4.0f)));    // 上外侧
    REQUIRE_FALSE(f.intersects(box(-0.5f, -60.0f, -5.0f, 0.5f, -50.0f, -4.0f)));  // 下外侧
}

TEST_CASE("Math: frustum boundary AABB epsilon margin", "[math][frustum]") {
    // far=50 → far 平面位于 z = 5-50 = -45；保守测试取「沿外法线投影最小的角点」
    // （n-vertex）：跨平面 AABB 恒判相交，须整个 AABB 越界才剔除。
    const Frustum f = Frustum::fromViewProj(makePerspectiveViewProj());
    // 恰贴 far 平面（n-vertex 与平面距离 0）：在裕度内 → 相交
    REQUIRE(f.intersects(box(-0.1f, -0.1f, -45.0005f, 0.1f, 0.1f, -45.0f)));
    // 整体越出 far 平面（实测翻转点位于 -45.01 与 -46.0 之间，取 1 米安全裕度）→ 剔除
    REQUIRE_FALSE(f.intersects(box(-0.1f, -0.1f, -46.0f, 0.1f, 0.1f, -45.95f)));
    // 跨平面（n-vertex 在内侧）→ 保守相交，不闪没
    REQUIRE(f.intersects(box(-0.1f, -0.1f, -45.005f, 0.1f, 0.1f, -44.995f)));
}

TEST_CASE("Math: orthographic frustum", "[math][frustum][ortho]") {
    const Frustum f = Frustum::fromViewProj(makeOrthoViewProj());
    REQUIRE(f.intersects(box(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f)));     // 视锥内
    REQUIRE(f.intersects(box(-100.0f, -100.0f, -100.0f, 100.0f, 100.0f, 100.0f)));  // 包裹
    REQUIRE_FALSE(f.intersects(box(100.0f, 0.0f, -5.0f, 101.0f, 1.0f, -4.0f)));  // 横向超出（正交无透视发散）
    REQUIRE_FALSE(f.intersects(box(0.0f, 0.0f, -60.0f, 1.0f, 1.0f, -55.0f)));    // 远于 far
    REQUIRE_FALSE(f.intersects(box(0.0f, 0.0f, 6.0f, 1.0f, 1.0f, 7.0f)));        // 相机背后
}

TEST_CASE("Math: degenerate AABB point and uninitialized", "[math][frustum]") {
    const Frustum f = Frustum::fromViewProj(makePerspectiveViewProj());
    // 单点 AABB（min == max）
    REQUIRE(f.intersects(box(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)));         // 视锥内点
    REQUIRE_FALSE(f.intersects(box(100.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f)));  // 视锥外点

    // 未初始化/倒置 AABB（min > max）：确定性结果、不崩溃（两次调用一致）
    AABB inverted;   // 默认构造 min=+FLT_MAX, max=-FLT_MAX
    const bool r1 = f.intersects(inverted);
    const bool r2 = f.intersects(inverted);
    REQUIRE(r1 == r2);
}
