#include <NovaEngine/Render/Frustum.h>

#include <glm/geometric.hpp>   // dot / length
#include <glm/vec3.hpp>

namespace NovaEngine {

namespace {

// 剔除安全裕度（世界单位）：世界 AABB 由 8 角点浮点变换取包围盒，存在舍入误差；
// 距平面不足该裕度的 AABB 视为相交，防止边缘物体因舍入在帧间闪出/闪没（验收项 2）
constexpr float kPlaneEpsilon = 1e-4f;

}  // namespace

Frustum Frustum::fromViewProj(const glm::mat4& viewProj) {
    Frustum f;
    // Gribb-Hartmann：6 平面 = viewProj 行向量组合。glm 列主序，viewProj[i] 是第 i 列，
    // 故手工拼出行向量（row_i = (m[0][i], m[1][i], m[2][i], m[3][i])）。
    // 组合式给出「内部半空间 dot(c, p) + d >= 0」的向内法线，取反得朝外法线。
    const glm::vec4 r0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
    const glm::vec4 r1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
    const glm::vec4 r2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
    const glm::vec4 r3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);
    f.m_planes[0] = -(r3 + r0);   // 左
    f.m_planes[1] = r0 - r3;      // 右
    f.m_planes[2] = -(r3 + r1);   // 下
    f.m_planes[3] = r1 - r3;      // 上
    f.m_planes[4] = -(r3 + r2);   // 近（GL 裁剪 z∈[-1,1]）
    f.m_planes[5] = r2 - r3;      // 远

    // 归一化：xyz 分量长度归一，w 同步缩放（平面距离保持世界单位）
    for (glm::vec4& plane : f.m_planes) {
        const float len = glm::length(glm::vec3(plane));
        if (len > 0.0f) plane /= len;
    }
    return f;
}

bool Frustum::intersects(const AABB& worldAabb) const {
    // 保守相交测试（法线朝外）：逐平面取沿外法线投影最小的角点（n-vertex，最靠近锥体内侧）。
    // 若该角点仍在正半空间（dot(n, p) + d > 0），则 8 角点全部在平面外侧 → 完全剔除。
    for (const glm::vec4& plane : m_planes) {
        glm::vec3 p = worldAabb.min;
        if (plane.x < 0.0f) p.x = worldAabb.max.x;
        if (plane.y < 0.0f) p.y = worldAabb.max.y;
        if (plane.z < 0.0f) p.z = worldAabb.max.z;
        if (glm::dot(glm::vec3(plane), p) + plane.w > kPlaneEpsilon) return false;
    }
    return true;
}

}  // namespace NovaEngine
