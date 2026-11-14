#version 450 core

// 顶点属性（与 Mesh 的交错布局一致）
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;

// 每帧相机数据（Renderer::beginFrame 填充，binding 0）
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

uniform mat4 uModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    // 法线变换用逆转置矩阵（统一缩放时与 mat3(uModel) 等价；mini 引擎场景以统一/均匀缩放为主）
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
    gl_Position = uViewProj * worldPos;
}
