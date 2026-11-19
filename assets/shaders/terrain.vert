#version 450 core

// V0.5-8 地形顶点（c6，§25.6）：高度图 R16 位移 + 4 层 splat 权重传递（权重在片元侧采样）。
// 高度采样 = GL_NEAREST + CLAMP_TO_EDGE：与 CPU 阴影网格（ShadowItem）及 Jolt
// HeightFieldShape 逐样本一致（滤波差异会导致阴影/碰撞与渲染面脱节）。
// 法线 = 高度图有限差分（边界钳制单侧差），与 Terrain.cpp computeNormalsInRegion 同方案。

layout(location = 0) in vec3 aPosition;   // 局部平面网格顶点 (x, 0, z)（y 由高度图位移）
layout(location = 1) in vec2 aUV;         // 网格坐标 0..1（行 0 = z 负半轴）

// 每帧相机数据（与 blinn_phong.vert 同一 UBO，binding 0；Renderer::beginFrame 填充）
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

uniform mat4 uModel;
uniform sampler2D uHeightMap;   // unit 9：R16 高度图（0..1，NEAREST + CLAMP）
uniform vec4 uTerrainSize;      // (worldSize.x, worldSize.y, 0, 0)（米；XZ）
uniform float uHeightScale;     // 0..1 高度值 → 米
uniform int uHeightRes;         // 高度图分辨率（texel 步长）

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vWorldXZ;              // 局部 XZ（米；层 UV = 该值 / tileSize）
out vec2 vUV;

void main() {
    float hC = texture(uHeightMap, aUV).r;
    vec2 texel = vec2(1.0 / float(uHeightRes));
    // 有限差分法线（CLAMP 边界单侧差，与 CPU 重算法线一致；见 blinn_phong.frag 光照互指）
    float hL = texture(uHeightMap, aUV - vec2(texel.x, 0.0)).r;
    float hR = texture(uHeightMap, aUV + vec2(texel.x, 0.0)).r;
    float hD = texture(uHeightMap, aUV - vec2(0.0, texel.y)).r;
    float hU = texture(uHeightMap, aUV + vec2(0.0, texel.y)).r;
    float dx = uTerrainSize.x / float(uHeightRes - 1);
    float dz = uTerrainSize.y / float(uHeightRes - 1);
    vec3 n = normalize(vec3((hL - hR) * uHeightScale * 0.5 / dx,
                            1.0,
                            (hD - hU) * uHeightScale * 0.5 / dz));

    vec3 localPos = vec3(aPosition.x, hC * uHeightScale, aPosition.z);
    vec4 worldPos = uModel * vec4(localPos, 1.0);
    vWorldPos = worldPos.xyz;
    vWorldXZ = aPosition.xz;
    vUV = aUV;

    // 法线变换用逆转置矩阵（blinn_phong.vert 同款；地形场景以统一缩放为主）
    mat3 normalMat = mat3(transpose(inverse(uModel)));
    vNormal = normalize(normalMat * n);

    gl_Position = uViewProj * worldPos;
}
