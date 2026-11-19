#version 450 core

// V0.5-8 地形片元（c6，§25.6）：4 层 albedo/normal 按 splat 权重混合 + 自含 GGX 光照。
// GGX D/G/F、阴影 PCF、IBL 公式与 blinn_phong.frag 同源（两文件互指：改光照公式必须同步）。
// 贴图 unit 约定（本 shader）：0-3 层 albedo / 4-7 层 normal / 8 splat / 9 height / 1 shadow。
// 注意：unit 1 与第 2 层 albedo 共址——本波 C++ 侧 uShadowEnabled 恒 0（钩子点在 shadow pass
// 之前，无阴影状态访问器，见 TerrainRenderer.cpp 接缝注释），shadow 采样不执行故无冲突；
// 合波日接通接收阴影后需把 albedo 移往 unit 10-13。

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vWorldXZ;
in vec2 vUV;

layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

// ===== 灯光（blinn_phong.frag 同语义；TerrainRenderer 按场景灯光填充）=====
uniform vec3 uLightDir;       // 指向光源
uniform vec3 uLightColor;     // 已乘 intensity
uniform vec3 uAmbient;

// ===== 4 层材质（splat R/G/B/A 对应 0..3；空路径 = 白/无，C++ 侧开关置 0）=====
uniform int uUseAlbedoMap[4];
uniform sampler2D uAlbedoMap[4];   // unit 0-3
uniform int uUseNormalMap[4];
uniform sampler2D uNormalMap[4];   // unit 4-7
uniform vec4 uTileSize;            // 每层平铺尺寸（米/次）
uniform sampler2D uSplatMap;       // unit 8：RGBA8 权重图（空 = 全权重在第 0 层）

// ===== 地形固定材质参数（v1 无逐层粗糙度/金属度）=====
uniform float uRoughness;   // 0.9
uniform float uMetallic;    // 0

// ===== 阴影（V0.2-6 约定，unit 1；本波 C++ 恒关，公式保留供合波日接通）=====
uniform sampler2D uShadowMap;
uniform mat4 uLightViewProj;
uniform int uShadowEnabled;
uniform float uShadowBias;
uniform float uShadowMapSize;

// ===== IBL 环境（V0.5-4，unit 6/7/8；与 blinn_phong.frag 同采样约定）=====
uniform samplerCube uIrrMap;
uniform samplerCube uPrefMap;
uniform sampler2D uBrdfLut;
uniform float uIblIntensity;
uniform float uIblSkyRotation;

uniform float uGamma;   // 同 blinn_phong：无后处理 1/2.2；HDR 0 直通

out vec4 fragColor;

const float PI = 3.14159265359;

// 以下 shadowFactor / distributionGGX / geometrySmith / fresnelSchlick 与
// blinn_phong.frag 同公式（互指：改动需两处同步）。
float shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
    vec4 lightSpacePos = uLightViewProj * vec4(worldPos, 1.0);
    if (lightSpacePos.w <= 0.0) return 1.0;
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    if (abs(proj.x) > 1.0 || abs(proj.y) > 1.0 || proj.z > 1.0) return 1.0;
    vec2 uv = proj.xy * 0.5 + 0.5;
    float current = proj.z * 0.5 + 0.5;
    float bias = max(uShadowBias * (1.0 - abs(dot(normalize(normal), lightDir))), uShadowBias * 0.5);
    float texel = 1.0 / uShadowMapSize;
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float closest = texture(uShadowMap, uv + vec2(float(x), float(y)) * texel).r;
            shadow += (closest < current - bias) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-4);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float k = roughness * roughness * 0.5;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // ===== 4 层权重（splat 归一化；空贴图时 C++ 侧绑定 1×1 全 R 白图 = 全权重第 0 层）=====
    vec4 w = texture(uSplatMap, vUV);
    float wSum = w.r + w.g + w.b + w.a;
    vec4 wn = wSum > 1e-5 ? w / wSum : vec4(1.0, 0.0, 0.0, 0.0);

    // ===== 4 层 albedo/normal 混合 =====
    vec3 albedo = vec3(0.0);
    vec3 nm = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        vec2 layerUV = vWorldXZ / max(uTileSize[i], 1e-3);
        vec3 layerAlbedo =
            uUseAlbedoMap[i] != 0 ? texture(uAlbedoMap[i], layerUV).rgb : vec3(1.0);
        albedo += wn[i] * layerAlbedo;
        if (uUseNormalMap[i] != 0) {
            vec3 tn = texture(uNormalMap[i], layerUV).rgb * 2.0 - 1.0;
            nm += wn[i] * tn;
        }
    }
    albedo = max(albedo, vec3(0.0));

    // 法线：无任何层法线贴图 → 顶点法线；有 → TBN 变换（blinn_phong.frag 同款右手系）
    vec3 N = normalize(vNormal);
    if (dot(nm, nm) > 1e-6) {
        nm = normalize(nm);
        vec3 helper = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 T = normalize(cross(helper, N));
        vec3 B = cross(N, T);
        N = normalize(T * nm.x + B * nm.y + N * nm.z);
    }

    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCamPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // 金属工作流（与 blinn_phong.frag 同公式；地形固定 metallic=0 → F0=0.04）
    vec3 F0 = mix(vec3(0.04), albedo, uMetallic);
    vec3 F = fresnelSchlick(HdotV, F0);
    float D = distributionGGX(NdotH, uRoughness);
    float G = geometrySmith(NdotV, NdotL, uRoughness);
    vec3 specular = F * D * G / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - uMetallic);
    vec3 direct = (kD * albedo / PI + specular) * uLightColor * NdotL;

    // 阴影只作用于直射光（本波 C++ 恒 0；公式保留供合波日接通）
    float shadow = uShadowEnabled != 0 ? shadowFactor(vWorldPos, N, L) : 1.0;

    vec3 ambient = uAmbient * albedo;

    // ===== IBL 间接光（blinn_phong.frag 同款；uIblIntensity=0 → 贡献 0）=====
    vec3 indirect = vec3(0.0);
    if (uIblIntensity > 0.0) {
        float sr = sin(uIblSkyRotation);
        float cr = cos(uIblSkyRotation);
        vec3 rotN = vec3(cr * N.x + sr * N.z, N.y, -sr * N.x + cr * N.z);
        vec3 R = reflect(-V, N);
        vec3 rotR = vec3(cr * R.x + sr * R.z, R.y, -sr * R.x + cr * R.z);
        vec3 irradiance = texture(uIrrMap, rotN).rgb;
        vec3 prefiltered = textureLod(uPrefMap, rotR, uRoughness * 4.0).rgb;
        vec2 brdf = texture(uBrdfLut, vec2(NdotV, uRoughness)).rg;
        vec3 specularIbl = prefiltered * (F * brdf.x + brdf.y);
        indirect = (kD * irradiance * albedo + specularIbl) * uIblIntensity;
    }

    vec3 color = ambient + direct * shadow + indirect;

    // 输出 gamma（blinn_phong.frag 同款：uGamma<=0 → HDR 直通分支）
    color = uGamma <= 0.0 ? color : pow(max(color, vec3(0.0)), vec3(uGamma));
    fragColor = vec4(color, 1.0);
}
