#version 450 core

in vec3 vWorldPos;
in float vMajor;

uniform vec3 uCamPos;
uniform vec3 uFogColor;    // 远处淡出颜色（与天空色一致）
uniform vec3 uLineColor;

out vec4 fragColor;

void main() {
    // 按到相机的距离淡出（40m 网格：15m 开始衰减，35m 完全融入雾色）
    float dist = distance(vWorldPos, uCamPos);
    float fade = 1.0 - smoothstep(15.0, 35.0, dist);

    // 主次线颜色区分：主线更亮、次线更暗
    vec3 line = vMajor > 0.5 ? uLineColor * 1.6 : uLineColor * 0.55;
    vec3 color = mix(uFogColor, line, fade);
    fragColor = vec4(color, 1.0);
}
