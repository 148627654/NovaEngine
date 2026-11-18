#version 450 core

// BRDF 积分 LUT（IBL，V0.5-4 §25.2）：全局一张 512²，与具体 HDRI 无关（IBL.cpp 首用生成，
// 全屏三角形渲染，顶点源码内嵌于 IBL.cpp——本文件只有片元段）。
// x 轴 = NdotV，y 轴 = roughness；输出 (scale A, bias B) → GL_RG16F 附件。
// blinn_phong.frag 消费约定：specularIbl = prefiltered * (F * brdf.x + brdf.y)。
// G 项与 blinn_phong.frag 的 geometrySmith 同约定（Smith-Schlick，k = α²/2，α = roughness²）。
// 无 gamma（线性中间缓冲）。

// LUT 尺寸固定 512²（与 IBL.cpp 的 kLutSize 一致；IBL.cpp 按同尺寸设视口渲染，
// gl_FragCoord 逐纹素对齐，故无需 uniform——Shader 类没有 setVec2）
const vec2 kLutSize = vec2(512.0);

out vec2 fragColor;

const float PI = 3.14159265359;
const int kSampleCount = 256;

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

// Smith-Schlick GGX 几何遮蔽（k = α²/2，与 blinn_phong.frag 同约定）
float geometrySchlickGgx(float NdotV, float roughness) {
    float k = roughness * roughness * 0.5;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGgx(NdotV, roughness) * geometrySchlickGgx(NdotL, roughness);
}

vec2 integrateBrdf(float NdotV, float roughness) {
    // 切线空间：N = +Z，V 由 NdotV 唯一确定（方位角无关，旋转对称）
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    float A = 0.0;   // 菲涅尔 scale
    float B = 0.0;   // 菲涅尔 bias
    for (int i = 0; i < kSampleCount; ++i) {
        vec2 xi = hammersley(i, kSampleCount);
        vec3 H = importanceSampleGgx(xi, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float HdotV = max(dot(H, V), 0.0);
        if (NdotL > 0.0) {
            float G = geometrySmith(NdotV, NdotL, roughness);
            float GVis = (G * HdotV) / max(NdotH * NdotV, 1e-5);
            float Fc = pow(1.0 - HdotV, 5.0);   // Schlick 菲涅尔（F0 提公因式后的系数）
            A += (1.0 - Fc) * GVis;
            B += Fc * GVis;
        }
    }
    return vec2(A, B) / float(kSampleCount);
}

void main() {
    vec2 uv = gl_FragCoord.xy / kLutSize;
    fragColor = integrateBrdf(uv.x, uv.y);
}
