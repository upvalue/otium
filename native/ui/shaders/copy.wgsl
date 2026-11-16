// Draws an offscreen target over the whole window.

@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(1) var image_sampler: sampler;

struct Copy {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
}

// One triangle covering the target; uv runs 0 to 1 across it.
@vertex
fn copy_vertex(@builtin(vertex_index) index: u32) -> Copy {
  let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
  var out: Copy;
  out.uv = uv;
  out.position = vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
  return out;
}

@fragment
fn copy_fragment(in: Copy) -> @location(0) vec4f {
  return textureSample(image, image_sampler, in.uv);
}
