#version 450 core

// 后处理合成（V0.3-14）：scene + bloom×intensity → Narkowicz ACES → exposure → gamma 1/2.2
// 输出到 LDR 目标（RGBA8）；场景为线性 HDR 输入（场景 pass 已 uGamma=0 直通）

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;         // 场景 HDR resolve 纹理
uniform sampler2D uBloom;         // bloom 链最终级（半/1/4/1/8 分辨率，双线性上采样）
uniform sampler2D uAmbientOcclusionTexture;   // AO（V0.5-5 SSAO）；0 = C++ 侧绑 1×1 白（乘 1）
uniform float uBloomIntensity;
uniform float uExposure;
// ===== V1.0-4 色彩分级（§27.3 骨架日布；缺省全关 = 逐像素旧行为）=====
// 注：uLut 为 sampler3D（unit 8）——C++ 侧必须常驻绑定：无 LUT 时绑 1×1 白 3D 兜底
//（永不黑屏契约；实现归 cG 的 ColorGrading）
uniform sampler3D uLut;
uniform float uLutIntensity;         // 0 = 直通
uniform float uVignetteIntensity;    // 0 = 关
uniform float uGrainAmount;          // 0 = 关
uniform float uGrainSeed;            // 每帧时间抖动种子（C++ 侧每帧传）
// ===== V1.0-4 直通路径一致性（§27.7-4，cG 最小补）：1 = RGBA8 直通模式 =====
// 输入已是 sRGB 显示编码：跳过 ACES/曝光，先线性化再走 LUT/渐晕/颗粒段
//（LUT 采样域与 HDR 路径一致），末段 gamma 重编码回显示域。
uniform int uDirectPassthrough;      // 0（默认）= HDR 链；1 = RGBA8 直通（仅 LUT/渐晕/颗粒生效）

vec3 acesTonemap(vec3 x) {
    // Narkowicz ACES 拟合（© Krzysztof Narkowicz，公开近似式）
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 scene = texture(uScene, vUv).rgb;
    vec3 bloom = texture(uBloom, vUv).rgb * uBloomIntensity;
    // AO 合成（V0.5-5，§25.3）：0 = 乘 1（C++ 侧兜底白纹理，AO 无贡献）
    float ao = texture(uAmbientOcclusionTexture, vUv).r;
    vec3 color = (scene + bloom) * ao;
    if (uDirectPassthrough != 0)
        color = pow(max(color, vec3(0.0)), vec3(2.2));   // sRGB → 线性（LUT 域一致，§27.7-4）
    else
        color = acesTonemap(color * uExposure);          // 曝光 → 色调映射
    // ===== V1.0-4 LUT / 渐晕 / 颗粒（§27.3 位置裁定：ACES 后、gamma 前；全关 = 旧行为）=====
    if (uLutIntensity > 0.0) {
        vec3 lutColor = texture(uLut, clamp(color, 0.0, 1.0)).rgb;   // 线性 LDR 域采样
        color = mix(color, lutColor, uLutIntensity);
    }
    if (uVignetteIntensity > 0.0) {
        float d = distance(vUv, vec2(0.5));
        color *= 1.0 - uVignetteIntensity * smoothstep(0.3, 1.0, d);
    }
    if (uGrainAmount > 0.0) {
        float n = fract(sin(dot(vUv * 437.7 + vec2(uGrainSeed), vec2(12.9898, 78.233))) * 43758.5453);
        color += (n - 0.5) * uGrainAmount;
    }
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));  // gamma 校正（sRGB 显示）
    fragColor = vec4(color, 1.0);
}
