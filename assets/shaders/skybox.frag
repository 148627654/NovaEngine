#version 450 core

// 天空（V0.2-7）：渐变与 cubemap 采样共用一个片元着色器（uUseCubemap 分支）。

in vec3 vWorldDir;

uniform int uUseCubemap;          // 0 = 程序化渐变，1 = cubemap 采样
uniform vec3 uHorizonColor;       // 渐变地平线色
uniform vec3 uZenithColor;        // 渐变天顶色
uniform float uSkyExposure;       // cubemap（HDR）曝光
uniform float uSkyRotation;       // cubemap 绕 +Y 旋转（弧度，C++ 侧已由度转换）
uniform samplerCube uSkyCubemap;  // unit 1

// ===== 输出 gamma（V0.3-4，与 blinn_phong.frag 同步）=====
// 无后处理：1/2.2（V0.2 现行为）；HDR 模式：0（触发直通分支，非 pow 指数 0；波 3 后处理接管，届时 C++ 侧改设）
uniform float uGamma;

out vec4 fragColor;

void main() {
    vec3 dir = normalize(vWorldDir);
    vec3 color;

    if (uUseCubemap != 0) {
        // 采样方向绕 +Y 旋转（旋转天空而非相机）
        float s = sin(uSkyRotation);
        float c = cos(uSkyRotation);
        vec3 rot = vec3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z);
        color = texture(uSkyCubemap, rot).rgb * uSkyExposure;
    } else {
        // 程序化渐变：地平线 → 天顶，pow 1.5 让地平线区域更亮
        float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
        t = pow(t, 1.5);
        color = mix(uHorizonColor, uZenithColor, t);
    }

    // 输出前 gamma 校正（引擎管线按线性计算，显示器按 sRGB 显示；uGamma=1/2.2 现行为）
    // uGamma<=0（HDR 模式，波 3 启用）走直通分支——pow(x,0)≡1 会全白，故必须显式分支
    color = uGamma <= 0.0 ? color : pow(max(color, vec3(0.0)), vec3(uGamma));
    fragColor = vec4(color, 1.0);
}
