#version 450 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

// 每帧相机数据（与顶点着色器同一 UBO，binding 0）
layout(std140, binding = 0) uniform CameraBlock {
    mat4 uViewProj;
    vec4 uCamPos;
};

uniform vec3 uLightDir;       // 指向光源（DirectionalLightData.direction 语义）
uniform vec3 uLightColor;     // 已乘 intensity
uniform vec3 uAmbient;
uniform vec3 uAlbedo;
uniform float uRoughness;
uniform float uMetallic;
uniform int uUseAlbedoMap;
uniform sampler2D uAlbedoMap; // unit 0

// ===== 阴影（V0.2-6）=====
uniform sampler2D uShadowMap;   // 深度贴图（unit 1；CLAMP_TO_BORDER + 白色边框）
uniform mat4 uLightViewProj;    // 光空间矩阵（已纹素对齐）
uniform int uShadowEnabled;     // 0 = 无阴影（无阴影帧由 C++ 关掉）
uniform float uShadowBias;      // 深度偏置（[0,1] 深度空间；默认 0.0005 约合数厘米世界距离）
uniform float uShadowMapSize;   // 深度贴图边长（texel 步长用）

out vec4 fragColor;

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

void main() {
    vec3 albedo = uUseAlbedoMap != 0 ? texture(uAlbedoMap, vUV).rgb * uAlbedo : uAlbedo;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCamPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    // 漫反射：金属度越高漫反射越弱
    vec3 diffuse = uLightColor * albedo * max(dot(N, L), 0.0) * (1.0 - uMetallic);

    // Blinn-Phong 镜面：粗糙度 0..1 映射到高光指数 256..2
    float shininess = mix(256.0, 2.0, uRoughness);
    float spec = pow(max(dot(N, H), 0.0), shininess);
    vec3 specColor = mix(vec3(0.04), albedo, uMetallic);   // 金属高光带底色，非金属灰白
    vec3 specular = uLightColor * specColor * spec;

    // 阴影只作用于直射光（环境光不受影）
    float shadow = uShadowEnabled != 0 ? shadowFactor(vWorldPos, N, L) : 1.0;
    vec3 color = uAmbient * albedo + (diffuse + specular) * shadow;

    // 输出前 gamma 校正（引擎管线按线性计算，显示器按 sRGB 显示）
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
