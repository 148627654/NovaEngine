#version 450 core

// DOF 简版（V1.0-4，§27.3）：单 shader 双 pass（uHorizontal 开关）。
// 链位置（冻结）：brightpass → bloom → DOF → combine{ACES → LUT → vignette → grain → gamma}。
//
// pass 1（uHorizontal=1，目标 = 半分辨率 RGBA16F）：
//   读全分辨率场景（unit 0）+ 线性深度（unit 1，视空间 -z 米）→ CoC → 量化 8 档 →
//   水平 9-tap 盒式模糊（半径按档位缩放）；输出 rgb = 水平模糊结果、a = 档位/7。
// pass 2（uHorizontal=0，目标 = 全分辨率 RGBA16F）：
//   垂直 9-tap 盒式模糊半分辨率中间图（半分辨率 texel = 2 全分辨率 texel）→
//   按档位权重 mix（档位 0 = 焦点上，完全保留锐利场景）→ 输出合成结果。
// 开销目标 <1ms @1080p（半分辨率 pass + 全分辨率 9-tap 合计约 2.7 千万次采样）。

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;         // 全分辨率场景（pass1 模糊源 / pass2 合成基准）
uniform sampler2D uDepth;         // 全分辨率线性深度（MRT 附件 2 resolve，RGBA16F）
uniform sampler2D uDofHalf;       // 半分辨率中间图（pass2 输入）
uniform int uHorizontal;          // 1 = 水平 pass；0 = 垂直 + 合成
uniform vec3 uTexelSize;          // 全分辨率 1/w, 1/h
uniform float uDofFocusDistance;  // 焦点距离（米）
uniform float uDofAperture;       // 光圈（CoC 缩放；0 = DOF 无效果）

// CoC = f(深度, focusDistance, aperture)：与焦点偏差 × 光圈 × 常数，钳制 [-1,1]
float cocFromDepth(float depth) {
    return clamp((depth - uDofFocusDistance) * uDofAperture * 40.0, -1.0, 1.0);
}

void main() {
    if (uHorizontal != 0) {
        // ===== pass 1：CoC + 水平模糊（半分辨率目标）=====
        float depth = texture(uDepth, vUv).r;
        float coc = cocFromDepth(depth);
        float level = floor(min(abs(coc), 1.0) * 7.0 + 0.5);   // 半径量化 8 档（0..7）
        float radius = level * 1.5;                            // 半分辨率 texel 单位
        vec3 sum = texture(uScene, vUv).rgb;
        for (int i = 1; i <= 4; ++i) {
            vec2 off = vec2(uTexelSize.x * float(i) * radius, 0.0);
            sum += texture(uScene, vUv + off).rgb;
            sum += texture(uScene, vUv - off).rgb;
        }
        sum /= 9.0;
        fragColor = vec4(sum, level / 7.0);   // 档位存 alpha（半分辨率 RGBA16F 无损）
    } else {
        // ===== pass 2：垂直模糊 + 合成（全分辨率目标）=====
        vec3 sharp = texture(uScene, vUv).rgb;
        float level = texture(uDofHalf, vUv).a * 7.0;   // 半分辨率线性采样（档位回读）
        float radius = level * 1.5;
        vec3 sum = texture(uDofHalf, vUv).rgb;
        for (int i = 1; i <= 4; ++i) {
            // 半分辨率 texel 步长 = 全分辨率 2 texel
            vec2 off = vec2(0.0, uTexelSize.y * 2.0 * float(i) * radius);
            sum += texture(uDofHalf, vUv + off).rgb;
            sum += texture(uDofHalf, vUv - off).rgb;
        }
        sum /= 9.0;
        float weight = clamp(level / 7.0, 0.0, 1.0);
        fragColor = vec4(mix(sharp, sum, weight), 1.0);
    }
}
