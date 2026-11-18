#version 450 core

// 镜面预滤波（IBL，V0.5-4 §25.2）：IBL.cpp 用 equirect_to_cube.vert 渲染单位立方体 6 面，
// 逐 mip 逐面（128² 起 5 级，roughness = mip / (mipCount-1) → LOD 0..4）。
// GGX 重要性采样（H 经 TBN 变换到世界空间），PDF 驱动源 cubemap 的 mip 选择防欠采样噪声。
// 输出 GL_RGB16F，无 gamma（线性中间缓冲）。

in vec3 vLocalPos;

uniform samplerCube uEnvironmentMap;   // unit 0：环境 cubemap（含 mip 链）
uniform float uRoughness;              // 当前 mip 对应粗糙度（0..1）
uniform float uSourceFaceSize;         // 源 cubemap 面边长（样本立体角 → mip 换算）

out vec4 fragColor;

const float PI = 3.14159265359;
const int kSampleCount = 128;

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

// GGX 重要性采样：返回半程向量 H（切线空间，N = +Z）
vec3 importanceSampleGgx(vec2 xi, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// GGX 采样 PDF = D * NdotH
float ggxPdf(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 * NdotH / max(PI * d * d, 1e-4);
}

void main() {
    vec3 N = normalize(vLocalPos);
    vec3 V = N;   // 卷积约定：观察方向 = 出射方向 = 反射方向（V = R）

    // 切线空间基（N 接近 ±Y 时换参考轴防退化）
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    if (uRoughness < 0.01) {
        // 镜面极限（mip 0）：单方向采样
        prefiltered = textureLod(uEnvironmentMap, V, 0.0).rgb;
    } else {
        // 源 cubemap 每纹素立体角：PDF 很低的样本从高 mip 采，防欠采样噪声
        float saTexel = 4.0 * PI / (6.0 * uSourceFaceSize * uSourceFaceSize);
        for (int i = 0; i < kSampleCount; ++i) {
            vec2 xi = hammersley(i, kSampleCount);
            vec3 H = importanceSampleGgx(xi, uRoughness);
            vec3 L = normalize(2.0 * dot(V, H) * H - V);   // 反射方向（切线空间内）
            float NdotL = max(dot(N, L), 0.0);
            if (NdotL > 0.0) {
                float NdotH = max(dot(N, H), 0.0);
                float pdf = ggxPdf(NdotH, uRoughness);
                float saSample = 1.0 / (float(kSampleCount) * pdf + 0.0001);
                float mipLevel = 0.5 * log2(saSample / saTexel);
                prefiltered += textureLod(uEnvironmentMap, L, mipLevel).rgb * NdotL;
                totalWeight += NdotL;
            }
        }
        prefiltered /= max(totalWeight, 1e-4);
    }
    fragColor = vec4(prefiltered, 1.0);
}
