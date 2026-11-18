#version 450 core

// V0.3-4：Blinn-Phong → Cook-Torrance GGX PBR（金属工作流）整体重写。
// 文件名不变：drawOutline 第一遍 / 透明队列共用此路径；grid 沿用同一 uniform 约定（uGamma 同步，§14.4）。
// 贴图 unit 总表：0 albedo / 1 shadow / 2 normal / 3 metallic / 4 roughness / 5 emission。

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;

// 每帧相机数据（与顶点着色器同一 UBO，binding 0）
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

// ===== 灯光（沿用 V0.2 语义）=====
uniform vec3 uLightDir;       // 指向光源（DirectionalLightData.direction 语义）
uniform vec3 uLightColor;     // 已乘 intensity
uniform vec3 uAmbient;

// ===== 材质 =====
uniform vec3 uAlbedo;
uniform float uRoughness;
uniform float uMetallic;
uniform float uAlpha;         // V0.2-8：AlphaBlend 混合系数（Opaque 恒 1.0）

// ===== 贴图开关 + 采样器（贴图 null 时 C++ 侧开关置 0）=====
uniform int uUseAlbedoMap;
uniform sampler2D uAlbedoMap;     // unit 0
uniform int uUseNormalMap;
uniform sampler2D uNormalMap;     // unit 2（法线贴图按 .novameta 线性加载）
uniform int uUseMetallicMap;
uniform sampler2D uMetallicMap;   // unit 3
uniform int uUseRoughnessMap;
uniform sampler2D uRoughnessMap;  // unit 4
uniform int uUseEmissionMap;
uniform sampler2D uEmissionMap;   // unit 5

// ===== 自发光 =====
uniform vec3 uEmissionColor;
uniform float uEmissionIntensity;

// ===== 阴影（V0.2-6，原样保留）=====
uniform sampler2D uShadowMap;   // 深度贴图（unit 1；CLAMP_TO_BORDER + 白色边框）
uniform mat4 uLightViewProj;    // 光空间矩阵（已纹素对齐）
uniform int uShadowEnabled;     // 0 = 无阴影（无阴影帧由 C++ 关掉）
uniform float uShadowBias;      // 深度偏置（[0,1] 深度空间；默认 0.0005 约合数厘米世界距离）
uniform float uShadowMapSize;   // 深度贴图边长（texel 步长用）

// ===== IBL 环境（V0.5-4，c0 骨架日布；贴图 unit 总表追加 6/7/8）=====
uniform samplerCube uIrrMap;    // unit 6：irradiance 卷积（32²）
uniform samplerCube uPrefMap;   // unit 7：prefiltered 镜面（128²，mip 链 5 级）
uniform sampler2D uBrdfLut;     // unit 8：BRDF 积分 LUT（512²）
uniform float uIblIntensity;    // 0 = 间接光 0（与 2a 前逐像素一致）

// ===== GBuffer 输出（V0.5-5 SSAO，c0 骨架日布）=====
layout(location = 1) out vec3 oNormal;       // 视图空间法线（RGBA16F 附件 1）
layout(location = 2) out float oLinearDepth; // 线性深度（view-space -z，RGBA16F 附件 2）
uniform int uGbufferPass;      // 0（默认）= 不写 GBuffer，off 时逐像素与原来一致
uniform mat4 uView;            // 视图矩阵（视图空间法线/线性深度换算）

// ===== 输出 gamma（V0.3-4）=====
// 无后处理：1/2.2（V0.2 现行为）；HDR 模式：0（触发直通分支，非 pow 指数 0；波 3 后处理接管，届时 C++ 侧改设）
uniform float uGamma;

out vec4 fragColor;

const float PI = 3.14159265359;

// PCF 3x3 手动比较 + 斜率自适应偏置；光空间盒外（白色边框 → 深度 1.0）返回 1.0（不受影）
float shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
    vec4 lightSpacePos = uLightViewProj * vec4(worldPos, 1.0);
    if (lightSpacePos.w <= 0.0) return 1.0;
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;   // [-1, 1]
    if (abs(proj.x) > 1.0 || abs(proj.y) > 1.0 || proj.z > 1.0) return 1.0;  // 盒外不受影
    vec2 uv = proj.xy * 0.5 + 0.5;
    float current = proj.z * 0.5 + 0.5;   // 深度映射到 [0, 1]
    // 斜率自适应偏置：表面越斜偏置越大（防 acne）
    float bias = max(uShadowBias * (1.0 - abs(dot(normalize(normal), lightDir))), uShadowBias * 0.5);
    float texel = 1.0 / uShadowMapSize;
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float closest = texture(uShadowMap, uv + vec2(float(x), float(y)) * texel).r;
            shadow += (closest < current - bias) ? 1.0 : 0.0;   // 标准方向：存储深度比接收点浅 = 被遮挡
        }
    return shadow / 9.0;
}

// 法线分布 D（GGX / Trowbridge-Reitz）
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-4);
}

// 几何遮蔽 G（Smith-Schlick GGX）
float geometrySmith(float NdotV, float NdotL, float roughness) {
    float k = roughness * roughness * 0.5;   // k = α/2，与 D 的 α=r² 重映射同一粗糙度约定（r 为 clamp 后的值）
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

// Fresnel-Schlick（金属工作流：F0 = mix(vec3(0.04), albedo, metallic)）
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 albedo = uUseAlbedoMap != 0 ? texture(uAlbedoMap, vUV).rgb * uAlbedo : uAlbedo;
    float metallic = uUseMetallicMap != 0 ? texture(uMetallicMap, vUV).r * uMetallic : uMetallic;
    float roughness = uUseRoughnessMap != 0 ? texture(uRoughnessMap, vUV).r * uRoughness : uRoughness;
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.045, 1.0);   // 下限防 roughness=0 时 D 项全零（镜面消失）

    // 法线：默认世界法线；有法线贴图时经 TBN 变换到世界空间。
    // T 投影去掉平行 N 的分量、B 由右手叉积重建——插值后重新正交化（右手系 TBN）
    vec3 N = normalize(vNormal);
    if (uUseNormalMap != 0) {
        vec3 tn = texture(uNormalMap, vUV).rgb * 2.0 - 1.0;
        vec3 T = normalize(vTangent - N * dot(vTangent, N));
        vec3 B = cross(N, T);
        N = normalize(T * tn.x + B * tn.y + N * tn.z);
    }

    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCamPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // 金属工作流：非金属 F0=0.04，金属 F0=albedo；金属无漫反射
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(HdotV, F0);
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 specular = F * D * G / max(4.0 * NdotV * NdotL, 1e-4);

    // 漫反射：Lambert（kD = (1-F)(1-metallic)，/PI 能量守恒）
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 direct = (kD * albedo / PI + specular) * uLightColor * NdotL;

    // 阴影只作用于直射光（环境光不受影）
    float shadow = uShadowEnabled != 0 ? shadowFactor(vWorldPos, N, L) : 1.0;

    // 环境光沿用 V0.2 语义（ambient * albedo）
    vec3 ambient = uAmbient * albedo;

    // ===== IBL 间接光（V0.5-4）：uIblIntensity=0 或未绑定 → 贡献 0，与 2a 前逐像素一致
    //（程序化回退 = 既有 ambientColor，不黑屏）=====
    vec3 indirect = vec3(0.0);
    if (uIblIntensity > 0.0) {
        vec3 R = reflect(-V, N);                       // 世界空间反射方向
        vec3 irradiance = texture(uIrrMap, N).rgb;     // 漫反射：irradiance 卷积（unit 6）
        // 镜面：prefiltered mip 链（unit 7，5 级）按粗糙度选 LOD（0..4）
        vec3 prefiltered = textureLod(uPrefMap, R, roughness * 4.0).rgb;
        // BRDF LUT（unit 8）：(NdotV, roughness) → (scale, bias) 补偿
        vec2 brdf = texture(uBrdfLut, vec2(NdotV, roughness)).rg;
        vec3 specularIbl = prefiltered * (F * brdf.x + brdf.y);
        indirect = (kD * irradiance * albedo + specularIbl) * uIblIntensity;
    }

    // 自发光（贴图 × 颜色 × 强度，线性空间相加）
    vec3 emission = (uUseEmissionMap != 0 ? texture(uEmissionMap, vUV).rgb : vec3(1.0))
                    * uEmissionColor * uEmissionIntensity;

    vec3 color = ambient + direct * shadow + indirect + emission;

    // ===== GBuffer 输出（V0.5-5 SSAO）：uGbufferPass=1 时写附件 1/2（法线/线性深度）；
    // 默认 0 = 该段不执行，逐像素与 2a 前一致 =====
    if (uGbufferPass != 0) {
        oNormal = mat3(uView) * N;                          // 世界法线 → 视图空间（RGBA16F 附件 1）
        oLinearDepth = -(uView * vec4(vWorldPos, 1.0)).z;   // view-space -z 线性深度（附件 2）
    }

    // 输出前 gamma 校正（引擎管线按线性计算，显示器按 sRGB 显示；uGamma=1/2.2 现行为）
    // uGamma<=0（HDR 模式，波 3 启用）走直通分支——pow(x,0)≡1 会全白，故必须显式分支
    color = uGamma <= 0.0 ? color : pow(max(color, vec3(0.0)), vec3(uGamma));
    fragColor = vec4(color, uAlpha);
}
