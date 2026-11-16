// Rounded rectangles and atlas masks for the UI, one instance per Rect.
// Matches rect.vert, rect.frag and rect.metal.

struct Viewport { size: vec4f }

@group(0) @binding(0) var<uniform> viewport: Viewport;
@group(0) @binding(1) var atlas: texture_2d<f32>;
@group(0) @binding(2) var atlas_sampler: sampler;

struct Instance {
  @location(0) rect: vec4f,
  @location(1) color: vec4f,
  @location(2) shape: vec4f,
  @location(3) clip: vec4f,
  @location(4) uv: vec4f,
}

struct Varyings {
  @builtin(position) position: vec4f,
  @location(0) local: vec2f,
  @location(1) pixel: vec2f,
  @location(2) color: vec4f,
  @location(3) shape: vec4f,
  @location(4) clip: vec4f,
  @location(5) texcoord: vec2f,
}

@vertex
fn rect_vertex(in: Instance, @builtin(vertex_index) index: u32) -> Varyings {
  var corners = array<vec2f, 6>(vec2f(0, 0), vec2f(1, 0), vec2f(0, 1),
                                vec2f(0, 1), vec2f(1, 0), vec2f(1, 1));
  let corner = corners[index];
  var out: Varyings;
  out.local = corner * (in.rect.zw + 2.0) - 1.0;
  if (in.shape.w > 0.0) {
    out.local = corner * in.rect.zw;
  }
  out.texcoord = in.uv.xy + corner * in.uv.zw;
  out.pixel = in.rect.xy + out.local;
  out.position = vec4f(out.pixel.x / viewport.size.x * 2.0 - 1.0,
                       1.0 - out.pixel.y / viewport.size.y * 2.0, 0.0, 1.0);
  out.color = in.color;
  out.shape = in.shape;
  out.clip = in.clip;
  return out;
}

@fragment
fn rect_fragment(in: Varyings) -> @location(0) vec4f {
  // Sampling and fwidth need uniform control flow, so both come first.
  let coverage = textureSample(atlas, atlas_sampler, in.texcoord).r;
  let halfsize = in.shape.xy * 0.5;
  let radius = min(in.shape.z, min(halfsize.x, halfsize.y));
  let q = abs(in.local - halfsize) - halfsize + radius;
  let distance = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - radius;
  let aa = max(fwidth(distance), 0.001);
  if (any(in.pixel < in.clip.xy) || any(in.pixel >= in.clip.zw)) {
    discard;
  }
  if (in.shape.w > 0.0) {
    let a = in.color.a * coverage;
    return vec4f(in.color.rgb * a, a);
  }
  let alpha = in.color.a * (1.0 - smoothstep(-aa * 0.5, aa * 0.5, distance));
  return vec4f(in.color.rgb * alpha, alpha);
}
