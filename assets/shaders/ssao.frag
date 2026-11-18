#version 450 core

// SSAO（V0.5-5，§25.3）：半球采样 → 半分辨率 AO（输出 RGBA16F，C++ 侧复用 post_blur 高斯模糊）
//
// 输入：场景法线（RGBA16F 附件 1，视图空间）+ 线性深度（RGBA16F 附件 2，view-space -z）。
// 相机参数：§25.3 冻结签名不传投影矩阵，本 shader 经 CameraBlock UBO（binding 0，
// Renderer 每帧填充 uViewProj/uCamPos）提取透视投影缩放——P 第 0/1 行 = 缩放 × V 旋转行
// （V 旋转行单位长 → 行长即缩放），据此重建视图坐标与回投屏幕 UV，无需 C++ 侧传矩阵。
// 天空/背景：GBuffer 未覆盖像素保持清屏色（法线模长 ≠ 1；几何法线经 mat3(uView)*N
// 严格单位长）→ 直接输出 AO=1（天空不被暗化，§25.3「永不黑屏」同纪律）。

in vec2 vUv;
out vec4 fragColor;

// 与 Renderer.cpp 的 CameraBlock 完全一致（std140：mat4 + vec4 = 80 字节）
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

uniform sampler2D uNormalTex;    // unit 0：视图空间法线
uniform sampler2D uDepthTex;     // unit 1：线性深度（view-space -z）
uniform sampler2D uNoiseTex;     // unit 2：4x4 随机旋转（RGBA8，REPEAT+NEAREST 平铺）
uniform float uRadius;           // 采样半径（米，view-space）
uniform float uStrength;         // 遮蔽强度
uniform int uSampleCount;        // 半球采样数（8/16/32）
uniform vec3 uNoiseScale;        // xy = AO 宽/4、高/4（4x4 噪声平铺系数）

// 固定半球核（仅依赖 i，全屏同构——每像素经噪声旋转切向基，无重复结构/条带；
// 黄金角螺旋 + 单位盘均匀半径：采样密度均匀、帧间稳定 → 静止画面无跳动）
vec3 kernelDir(int i) {
    float g = float(i) * 2.3999632;   // 黄金角（弧度）
    float r = sqrt((float(i) + 0.5) / float(max(uSampleCount, 1)));
    return vec3(cos(g) * r, sin(g) * r, sqrt(max(1.0 - r * r, 0.0)));
}

void main() {
    // ===== 正交相机守卫：透视投影 (P*V)[2][3] = -1（P 第 3 行 (0,0,-1,0)），正交 = 0。
    // 正交相机（c8 2D 域）本波不支持 → 输出 AO=1（无遮蔽，不产生伪影）=====
    if (abs(uViewProj[2][3]) < 0.5) { fragColor = vec4(1.0); return; }

    // ===== 投影缩放提取（透视）：P 第 0/1 行 = f/aspect、f 分别乘 V 第 0/1 行 =====
    float fx = length(vec3(uViewProj[0][0], uViewProj[1][0], uViewProj[2][0]));   // f/aspect
    float fy = length(vec3(uViewProj[0][1], uViewProj[1][1], uViewProj[2][1]));   // f
    if (fx < 1e-6 || fy < 1e-6) { fragColor = vec4(1.0); return; }   // 退化相机防御

    vec3 N = texture(uNormalTex, vUv).rgb;
    float d = texture(uDepthTex, vUv).r;

    // ===== 天空/背景判定：GBuffer 未写区域保持清屏色，法线模长 ≠ 1 =====
    if (abs(length(N) - 1.0) > 0.02 || d <= 0.0) { fragColor = vec4(1.0); return; }
    N = normalize(N);

    // ===== 视图空间位置重建（线性深度 + 投影缩放；无需逆投影矩阵）=====
    vec2 ndc = vUv * 2.0 - 1.0;
    vec3 viewPos = vec3(ndc.x / fx, ndc.y / fy, -1.0) * d;

    // ===== 切向基（噪声旋转；正交化防 N 与噪声投影平行退化）=====
    vec3 noise = texture(uNoiseTex, vUv * uNoiseScale.xy).xyz * 2.0 - 1.0;
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent0 = normalize(cross(up, N));
    vec3 rot = noise - N * dot(noise, N);
    vec3 tangent = length(rot) > 0.01 ? normalize(rot) : tangent0;
    vec3 bitangent = cross(N, tangent);
    mat3 tbn = mat3(tangent, bitangent, N);

    // ===== 半球采样 =====
    float occlusion = 0.0;
    const float bias = 0.025;   // 视空间深度偏置（防自遮蔽 acne）
    for (int i = 0; i < uSampleCount; ++i) {
        vec3 samplePos = viewPos + tbn * kernelDir(i) * uRadius;
        if (samplePos.z > -0.001) continue;   // 采样点跑到相机后方：跳过

        // 回投屏幕 UV（透视除法只依赖缩放项）；越界跳过（CLAMP 边缘采样会引入伪影）
        vec2 sampleUv = vec2(0.5 - 0.5 * fx * samplePos.x / samplePos.z,
                             0.5 - 0.5 * fy * samplePos.y / samplePos.z);
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0 ||
            sampleUv.y < 0.0 || sampleUv.y > 1.0) continue;

        // 采样点落在天空/背景（法线模长 ≠ 1）→ 不构成遮蔽（防轮廓外侧暗环）
        vec3 sampleNormal = texture(uNormalTex, sampleUv).rgb;
        if (abs(length(sampleNormal) - 1.0) > 0.02) continue;

        float sampleDepth = texture(uDepthTex, sampleUv).r;
        float sp = -samplePos.z;
        // 距离范围检查：采样点与表面深度差超过半径 → 平滑衰减（防远跳误遮蔽）
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(abs(sp - sampleDepth), 1e-6));
        occlusion += (sampleDepth < sp - bias ? 1.0 : 0.0) * rangeCheck;
    }
    float ao = 1.0 - uStrength * occlusion / float(max(uSampleCount, 1));
    fragColor = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
}
