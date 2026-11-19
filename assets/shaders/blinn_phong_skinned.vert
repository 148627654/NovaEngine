#version 450 core

// V0.5-9 蒙皮顶点着色器（§25.7，c7）：blinn_phong.vert + 骨骼蒙皮变换（4 骨骼权重混合）。
// 片元着色器复用 blinn_phong.frag（2a 后含 IBL——蒙皮角色自动获得 IBL 间接光），
// 输出 varyings 与 blinn_phong.vert 完全一致。

// 顶点属性（与 Mesh 蒙皮交错布局一致：0..3 同 Vertex，4/5 为骨骼索引/权重）
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in ivec4 aBoneIds;      // 无骨影响的顶点：全 0 + 权重全 0（shader 兜底绑定姿势）
layout(location = 5) in vec4 aBoneWeights;   // 权重和归一化由导入器保证（§25.7）

// 每帧相机数据（Renderer::beginFrame 填充，binding 0——全局绑定点，跨 program 共享）
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

// 骨骼矩阵 UBO（Mesh 持有，Animation 模块每帧上传；上限 64，§25.7）
layout(std140, binding = 1) uniform BoneBlock {
    mat4 uBoneMatrices[64];
};

uniform mat4 uModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out vec3 vBitangent;

void main() {
    // 蒙皮变换：4 骨骼权重混合（权重和为 0 = 无骨影响 → 绑定姿势恒等）
    mat4 skin = mat4(0.0);
    float weightSum = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;
    if (weightSum > 1e-3) {
        skin = uBoneMatrices[aBoneIds.x] * aBoneWeights.x
             + uBoneMatrices[aBoneIds.y] * aBoneWeights.y
             + uBoneMatrices[aBoneIds.z] * aBoneWeights.z
             + uBoneMatrices[aBoneIds.w] * aBoneWeights.w;
        skin /= weightSum;   // 容错归一化（导入器已保证）
    } else {
        skin = mat4(1.0);    // 绑定姿势
    }

    vec4 skinnedPos = skin * vec4(aPosition, 1.0);
    // 法线/切线用 skin 的 3x3（统一缩放近似；非均匀缩放由片元侧 normalize 兜底）
    vec3 skinnedNormal = mat3(skin) * aNormal;
    vec3 skinnedTangent = mat3(skin) * aTangent;

    vec4 worldPos = uModel * skinnedPos;
    vWorldPos = worldPos.xyz;

    // 世界变换法线（与 blinn_phong.vert 一致：逆转置）
    mat3 normalMat = mat3(transpose(inverse(uModel)));
    vec3 n = normalize(normalMat * skinnedNormal);
    vec3 t = normalMat * skinnedTangent;
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
