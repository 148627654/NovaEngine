#version 450 core

// V0.5-14 实例化顶点着色器：blinn_phong.vert + 实例矩阵属性。
// 复用 blinn_phong.frag；uniform 绑定与 setBlinnPhongUniforms 完全一致（uModel 由实例矩阵属性替代，
// setBlinnPhongUniforms 对 uModel 的设置因该 uniform 不存在而被安全跳过）。

// 顶点属性（与 Mesh 的交错布局一致；location 4..7 = 实例模型矩阵，divisor 1）
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in mat4 aInstanceModel;

// 每帧相机数据（Renderer::beginFrame 填充，binding 0）
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;     // V0.3-4：世界空间切线（法线贴图 TBN 用）
out vec3 vBitangent;   // V0.3-4：世界空间副切线（切线退化兜底/未来手性修正预留；片元侧现由 N×T 重建）

void main() {
    vec4 worldPos = aInstanceModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;

    // 法线变换用逆转置矩阵（统一缩放时与 mat3(aInstanceModel) 等价；mini 引擎场景以统一/均匀缩放为主）
    mat3 normalMat = mat3(transpose(inverse(aInstanceModel)));
    vec3 n = normalize(normalMat * aNormal);
    vec3 t = normalMat * aTangent;
    // 无切线数据的旧网格兜底：任取与法线不平行的轴构造正交切线（否则法线贴图 TBN 退化）
    if (dot(t, t) < 1e-6) {
        vec3 helper = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        t = cross(helper, n);
    }
    vNormal = n;
    vTangent = normalize(t);
    vBitangent = cross(n, vTangent);

    vUV = aUV;
    gl_Position = uViewProj * worldPos;
}
