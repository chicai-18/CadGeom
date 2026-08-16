#version 450

layout(location = 0) out vec2 vNdc;

// One oversized triangle rather than a quad: no vertex buffer, no index buffer,
// and no diagonal seam where two triangles meet. The grid itself is computed
// entirely in the fragment shader, so this is all the geometry the pass has
// (docs/architecture.md §4.2).
void main() {
    const vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vNdc = corner * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
