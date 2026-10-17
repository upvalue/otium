#version 450
layout(location=0) in vec2 local;
layout(location=1) in vec2 pixel;
layout(location=2) in vec4 color;
layout(location=3) in vec4 shape;
layout(location=4) in vec4 clip;
layout(location=0) out vec4 result;
void main() {
  if (any(lessThan(pixel, clip.xy)) || any(greaterThanEqual(pixel, clip.zw))) discard;
  vec2 halfsize = shape.xy * 0.5;
  float radius = min(shape.z, min(halfsize.x, halfsize.y));
  vec2 q = abs(local - halfsize) - halfsize + radius;
  float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
  float aa = max(fwidth(distance), 0.001);
  float alpha = color.a * (1.0 - smoothstep(-aa * 0.5, aa * 0.5, distance));
  result = vec4(color.rgb * alpha, alpha);
}
