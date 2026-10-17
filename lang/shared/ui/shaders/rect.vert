#version 450
layout(location=0) in vec4 rect;
layout(location=1) in vec4 color;
layout(location=2) in vec4 shape;
layout(location=3) in vec4 clip;
layout(set=1,binding=0) uniform Viewport { vec4 viewport; };
layout(location=0) out vec2 local;
layout(location=1) out vec2 pixel;
layout(location=2) out vec4 outColor;
layout(location=3) out vec4 outShape;
layout(location=4) out vec4 outClip;
void main() {
  vec2 corners[6] = vec2[](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
  local = corners[gl_VertexIndex] * (rect.zw + 2.0) - 1.0;
  pixel = rect.xy + local;
  gl_Position = vec4(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0, 0, 1);
  outColor = color; outShape = shape; outClip = clip;
}
