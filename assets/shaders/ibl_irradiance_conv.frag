#version 450 core

// 辐照度卷积（IBL 漫反射，V0.5-4 §25.2）：IBL.cpp 用 equirect_to_cube.vert 渲染单位立方体 6 面
//（相机在原点，深度保证近面覆盖远面），片元方向 = 归一化局部坐标。
// 余弦加权半球采样累加源环境 cubemap（unit 0，线性过滤）→ 输出 GL_RGB16F（32²，无 mip）。
// 无 gamma：输出目标是线性中间缓冲（RGB16F），HDR 值原样保留。

in vec3 vLocalPos;

uniform samplerCube uEnvironmentMap;   // unit 0：等距柱状转换后的环境 cubemap

out vec4 fragColor;

const float PI = 3.14159265359;
const int kSampleCount = 512;

// Hammersley 低差异序列（van der Corput 基 2 位反转）
float radicalInverseVdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;   // / 0x100000000
}

vec2 hammersley(int i, int n) {
    return vec2(float(i) / float(n), radicalInverseVdc(uint(i)));
}

// 余弦加权半球采样（切线空间，N = +Z）
vec3 sampleCosineHemisphere(vec2 xi) {
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

void main() {
    vec3 N = normalize(vLocalPos);

    // 切线空间基（N 接近 ±Y 时换参考轴防退化）
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    vec3 irradiance = vec3(0.0);
    for (int i = 0; i < kSampleCount; ++i) {
        vec2 xi = hammersley(i, kSampleCount);
        vec3 tangentSample = sampleCosineHemisphere(xi);
        vec3 dir = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
        irradiance += texture(uEnvironmentMap, dir).rgb;
    }
    // 余弦加权半球积分（×π 球面积分归一，平均即卷积结果）
    irradiance = PI * irradiance * (1.0 / float(kSampleCount));
    fragColor = vec4(irradiance, 1.0);
}
