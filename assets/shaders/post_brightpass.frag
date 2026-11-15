#version 450 core

// 亮度提取（V0.3-14）：阈值软截断（smoothstep 软膝）+ 2x2 平均降采样到半分辨率

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTexture;    // 场景 HDR resolve 纹理
uniform float uThreshold;      // 亮度阈值（线性空间）
uniform vec3 uTexelSize;       // xy = 源纹理 1/宽、1/高（Shader 无 setVec2，用 vec3 承载）

void main() {
    // 2x2 块平均（= 双线性中点，降采样 1/2）
    vec2 t = uTexelSize.xy * 0.5;
    vec3 c = texture(uTexture, vUv + vec2( t.x,  t.y)).rgb
           + texture(uTexture, vUv + vec2(-t.x,  t.y)).rgb
           + texture(uTexture, vUv + vec2( t.x, -t.y)).rgb
           + texture(uTexture, vUv + vec2(-t.x, -t.y)).rgb;
    c *= 0.25;

    // 软膝：阈值附近平滑过渡，避免硬截断闪烁
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float w = smoothstep(uThreshold, uThreshold + max(uThreshold * 0.5, 0.001), luma);

    fragColor = vec4(c * w, 1.0);
}
