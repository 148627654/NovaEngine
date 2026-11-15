#version 450 core

// 9-tap 高斯模糊（V0.3-14）：可分离方向核；uDirection=(1,0)/(0,1) 水平/垂直；
// 降采样级用 uDirection=(2,0)（stride 2，前级已各向同性 → 单方向即降采样+模糊）

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform vec3 uDirection;     // xy = 核方向（水平/垂直/stride）
uniform vec3 uTexelSize;     // xy = 源纹理 1/宽、1/高

void main() {
    // 高斯权重（sigma≈1.6，5 点对称 = 9-tap）
    const float w[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);
    vec3 c = texture(uTexture, vUv).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = uDirection.xy * uTexelSize.xy * float(i);
        c += texture(uTexture, vUv + off).rgb * w[i];
        c += texture(uTexture, vUv - off).rgb * w[i];
    }
    fragColor = vec4(c, 1.0);
}
