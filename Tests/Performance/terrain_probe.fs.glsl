#version 460 core
layout(location=2) in vec3 worldNormal;
layout(location=0) out vec4 color;
// Exercises production descriptor/vertex reads during generation. Intentionally
// omits material/lighting costs; this fixture measures upload contention only.
void main() { color=vec4(abs(worldNormal),1); }
