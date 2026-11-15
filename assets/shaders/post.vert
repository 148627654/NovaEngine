#version 450 core

// 后处理全屏三角形（V0.3-14）：无 VBO，gl_VertexID 生成 3 顶点覆盖全屏（glDrawArrays(GL_TRIANGLES, 0, 3)）

out vec2 vUv;

void main() {
    // 顶点 ID 0/1/2 → (-1,-1) (3,-1) (-1,3)，覆盖全屏（UV 0..1，y 向上）
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vUv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
