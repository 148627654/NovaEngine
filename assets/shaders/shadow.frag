#version 450 core

// 阴影深度 pass（V0.2-6）：深度由固定管线写入，FBO 无颜色附件（glDrawBuffer(GL_NONE)），
// 片元着色器无需输出任何颜色。

void main() {
    // 空实现：只写深度
}
