#version 450 core

layout(location = 0) in vec3 aPosition;   // 世界坐标线段端点（Renderer 预展开为 GL_LINES）
layout(location = 1) in float aMajor;     // 0 = 次线（1m），1 = 主线（5m）

uniform mat4 uViewProj;
uniform vec3 uCamPos;

out vec3 vWorldPos;
out float vMajor;

void main() {
    vWorldPos = aPosition;
    vMajor = aMajor;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
