#version 460

// Fullscreen triangle — no vertex buffer needed
// Generates a triangle that covers the entire screen

layout(location = 0) out vec2 uv;

void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    uv.y = 1.0 - uv.y;
}
