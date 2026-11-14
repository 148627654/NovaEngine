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

out vec4 fragColor;

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

    vec3 color = uAmbient * albedo + diffuse + specular;

    // 输出前 gamma 校正（引擎管线按线性计算，显示器按 sRGB 显示）
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
