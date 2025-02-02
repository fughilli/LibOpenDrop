#version 120

#include "preset/common/math.shh"

varying vec2 screen_uv;
varying vec2 texture_uv;
varying vec3 normal;

uniform sampler2D in_tex_a;
uniform sampler2D in_tex_b;
uniform float rotation_coeff;
uniform float zoom_coeff;
uniform vec2 zoom_center;

void main() {
  vec2 tex_a_uv = screen_to_tex(
      zoom_towards(rotate(screen_uv, rotation_coeff), zoom_coeff, zoom_center));
  vec2 tex_b_uv = screen_to_tex(screen_uv);

  vec4 in_color = texture2D(in_tex_b, tex_b_uv);

  gl_FragColor = mix(texture2D(in_tex_a, tex_a_uv), in_color, in_color.a);
}
