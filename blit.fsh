#version 120

uniform sampler2D source_texture;
uniform float alpha;
varying vec2 screen_uv;

vec2 screen_to_tex(vec2 screen_uv) { return (screen_uv + vec2(1., 1.)) * 0.5; }

void main() {
  vec2 tex_uv = screen_to_tex(screen_uv);
  gl_FragColor = texture2D(source_texture, tex_uv) * alpha;
}
