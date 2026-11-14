#version 450 core

// 天空（V0.2-7）：全屏三角形由 gl_VertexID 生成（空 VAO，无顶点缓冲）。
// z = w = 1.0 → 深度 1.0（最远）；配合 GL_LEQUAL 只画在已清屏区域，不遮挡任何物体。

// 每帧相机数据（Renderer::beginFrame 填充，binding 0）
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;      // 相机世界坐标（beginFrame 已填）
};

uniform mat4 uInvViewProj;   // 视投影逆矩阵（SkyboxRenderer::draw 传入）

out vec3 vWorldDir;

// 覆盖全屏的 3 个顶点（NDC）
const vec2 kFullscreenTriangle[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main() {
    const vec2 ndc = kFullscreenTriangle[gl_VertexID];
    gl_Position = vec4(ndc, 1.0, 1.0);

    // 世界空间视线方向：远平面世界坐标 - 相机位置（天空无穷远，与距离无关）
    vec4 worldFar = uInvViewProj * vec4(ndc, 1.0, 1.0);
    worldFar /= worldFar.w;
    vWorldDir = worldFar.xyz - uCamPos.xyz;
}
