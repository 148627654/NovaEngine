#version 450 core

// 等距柱状 → cubemap 转换（Cubemap::createFromEquirect 内部使用）。
// 无 gamma：输出目标为 GL_RGB16F（线性中间缓冲），HDR 值原样保留。

in vec3 vLocalPos;

uniform sampler2D uEquirectMap;   // unit 0

out vec4 fragColor;

const vec2 kInvAtan = vec2(0.1591, 0.3183);   // 1/(2π), 1/π

// 方向 → equirect UV（atan/asin 参数化；v 取负：stb 不翻转加载时 GL 纹理 v=0 为图像顶行，
// 天顶 dir.y=+1 应采样图像顶部 v=0）
vec2 sampleSphericalMap(vec3 dir) {
    vec2 uv = vec2(atan(dir.z, dir.x), -asin(dir.y));
    uv *= kInvAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec3 dir = normalize(vLocalPos);
    vec3 color = texture(uEquirectMap, sampleSphericalMap(dir)).rgb;
    fragColor = vec4(color, 1.0);
}
