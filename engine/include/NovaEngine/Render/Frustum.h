#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include "MeshData.h"   // AABB

namespace NovaEngine {

// 视锥剔除（V0.5-14）：Gribb-Hartmann 从 viewProj 提取 6 平面，法线朝锥体外。
// 纯数学类（仅依赖 GLM，无 GL 细节）；由 SceneRenderer 每帧从渲染相机构造。
class Frustum {
public:
    // 从 (view × projection) 提取左右/上下/近远 6 平面（内部归一化）。
    // 正交投影同样成立——w 行平面退化为平行面，无需特判（V0.5-10 预留）。
    static Frustum fromViewProj(const glm::mat4& viewProj);

    // true = AABB 部分或完全在锥体内（可渲染）；false = 完全在外（可剔除）。
    // 输入为世界空间 AABB（局部 AABB 8 角点经世界矩阵变换后取包围盒，保守精确）。
    bool intersects(const AABB& worldAabb) const;

private:
    // 归一化平面 (nx, ny, nz, d)，法线朝锥体外；锥体内部半空间满足 dot(n, p) + d <= 0
    glm::vec4 m_planes[6];
};

}  // namespace NovaEngine
