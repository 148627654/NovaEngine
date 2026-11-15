#version 450 core

in vec3 vWorldPos;
in float vMajor;

uniform vec3 uCamPos;
uniform vec3 uFogColor;    // 远处淡出颜色（与天空色一致）
uniform vec3 uLineColor;

// ===== 阴影（V0.2-6）=====
// shadowFactor 与 blinn_phong.frag 保持同步；V0.3 PBR 阶段合并
uniform vec3 uLightDir;         // 指向光源（DirectionalLightData.direction 语义）
uniform sampler2D uShadowMap;   // 深度贴图（unit 1；CLAMP_TO_BORDER + 白色边框）
uniform mat4 uLightViewProj;    // 光空间矩阵（已纹素对齐）
uniform int uShadowEnabled;     // 0 = 无阴影（无阴影帧由 C++ 关掉）
uniform float uShadowBias;      // 深度偏置（[0,1] 深度空间；默认 0.0005 约合数厘米世界距离）
uniform float uShadowMapSize;   // 深度贴图边长（texel 步长用）

// ===== 输出 gamma（V0.3-4，与 blinn_phong.frag 同步）=====
// 无后处理：1/2.2（V0.2 现行为）；HDR 模式：0（触发直通分支，非 pow 指数 0；波 3 后处理接管，届时 C++ 侧改设）
uniform float uGamma;

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
    // 按到相机的距离淡出（40m 网格：15m 开始衰减，35m 完全融入雾色）
    float dist = distance(vWorldPos, uCamPos);
    float fade = 1.0 - smoothstep(15.0, 35.0, dist);

    // 主次线颜色区分：主线更亮、次线更暗
    vec3 line = vMajor > 0.5 ? uLineColor * 1.6 : uLineColor * 0.55;

    // 地面接收阴影：网格为 y=0 平面，法线恒为 +Y；全影时行色压到 35%
    float shadow = uShadowEnabled != 0 ? shadowFactor(vWorldPos, vec3(0.0, 1.0, 0.0), uLightDir) : 1.0;
    line *= mix(0.35, 1.0, shadow);

    vec3 color = mix(uFogColor, line, fade);

    // 输出前 gamma 校正（线性计算；uGamma=1/2.2 现行为）
    // uGamma<=0（HDR 模式，波 3 启用）走直通分支——pow(x,0)≡1 会全白，故必须显式分支
    color = uGamma <= 0.0 ? color : pow(max(color, vec3(0.0)), vec3(uGamma));
    fragColor = vec4(color, 1.0);
}
