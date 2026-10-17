#include <metal_stdlib>
using namespace metal;
struct Input { float4 rect [[attribute(0)]]; float4 color [[attribute(1)]];
               float4 shape [[attribute(2)]]; float4 clip [[attribute(3)]]; };
struct Output { float4 position [[position]]; float2 local; float2 pixel;
                float4 color; float4 shape; float4 clip; };
vertex Output vertex_main(Input in [[stage_in]], uint vertex_index [[vertex_id]],
                           constant float4& viewport [[buffer(0)]]) {
  const float2 corners[] = {float2(0,0),float2(1,0),float2(0,1),
                            float2(0,1),float2(1,0),float2(1,1)};
  Output out;
  out.local = corners[vertex_index] * (in.rect.zw + 2.0) - 1.0;
  out.pixel = in.rect.xy + out.local;
  out.position = float4(out.pixel.x / viewport.x * 2.0 - 1.0,
                        1.0 - out.pixel.y / viewport.y * 2.0, 0, 1);
  out.color = in.color; out.shape = in.shape; out.clip = in.clip;
  return out;
}
fragment float4 fragment_main(Output in [[stage_in]]) {
  if (any(in.pixel < in.clip.xy) || any(in.pixel >= in.clip.zw)) discard_fragment();
  float2 halfsize = in.shape.xy * 0.5;
  float radius = min(in.shape.z, min(halfsize.x, halfsize.y));
  float2 q = abs(in.local - halfsize) - halfsize + radius;
  float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
  float aa = max(fwidth(distance), 0.001);
  float alpha = in.color.a * (1.0 - smoothstep(-aa * 0.5, aa * 0.5, distance));
  return float4(in.color.rgb * alpha, alpha);
}
