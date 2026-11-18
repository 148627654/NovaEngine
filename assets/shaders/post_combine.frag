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
    color = acesTonemap(color * uExposure);            // 曝光 → 色调映射
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));  // gamma 校正（sRGB 显示）
    fragColor = vec4(color, 1.0);
}
