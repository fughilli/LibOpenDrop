#include "preset/kaleidoscope/kaleidoscope.h"

#include <algorithm>
#include <cmath>

#include "debug/control_injector.h"
#include "preset/kaleidoscope/composite.fsh.h"
#include "preset/kaleidoscope/passthrough.vsh.h"
#include "preset/kaleidoscope/warp.fsh.h"
#include "preset/kaleidoscope/waveform.fsh.h"
#include "third_party/gl_helper.h"
#include "util/graphics/colors.h"
#include "util/graphics/gl_util.h"
#include "util/logging/logging.h"
#include "util/math/math.h"
#include "util/status/status_macros.h"

namespace opendrop {

namespace {
constexpr float kScaleFactor = 0.20f;
}

Kaleidoscope::Kaleidoscope(
    std::shared_ptr<gl::GlProgram> waveform_program,
    std::shared_ptr<gl::GlProgram> warp_program,
    std::shared_ptr<gl::GlProgram> composite_program,
    std::shared_ptr<gl::GlRenderTarget> front_render_target,
    std::shared_ptr<gl::GlRenderTarget> back_render_target,
    std::shared_ptr<gl::GlTextureManager> texture_manager)
    : Preset(texture_manager),
      waveform_program_(waveform_program),
      warp_program_(warp_program),
      composite_program_(composite_program),
      front_render_target_(front_render_target),
      back_render_target_(back_render_target) {}

absl::StatusOr<std::shared_ptr<Preset>> Kaleidoscope::MakeShared(
    std::shared_ptr<gl::GlTextureManager> texture_manager) {
  ASSIGN_OR_RETURN(
      auto waveform_program,
      gl::GlProgram::MakeShared(passthrough_vsh::Code(), waveform_fsh::Code()));
  ASSIGN_OR_RETURN(
      auto warp_program,
      gl::GlProgram::MakeShared(passthrough_vsh::Code(), warp_fsh::Code()));
  ASSIGN_OR_RETURN(auto composite_program,
                   gl::GlProgram::MakeShared(passthrough_vsh::Code(),
                                             composite_fsh::Code()));
  ASSIGN_OR_RETURN(auto front_render_target,
                   gl::GlRenderTarget::MakeShared(0, 0, texture_manager));
  ASSIGN_OR_RETURN(auto back_render_target,
                   gl::GlRenderTarget::MakeShared(0, 0, texture_manager));

  return std::shared_ptr<Kaleidoscope>(new Kaleidoscope(
      waveform_program, warp_program, composite_program, front_render_target,
      back_render_target, texture_manager));
}

void Kaleidoscope::OnUpdateGeometry() {
  if (front_render_target_ != nullptr) {
    front_render_target_->UpdateGeometry(longer_dimension(),
                                         longer_dimension());
  }
  if (back_render_target_ != nullptr) {
    back_render_target_->UpdateGeometry(longer_dimension(), longer_dimension());
  }
}

void Kaleidoscope::OnDrawFrame(
    absl::Span<const float> samples, std::shared_ptr<GlobalState> state,
    float alpha, std::shared_ptr<gl::GlRenderTarget> output_render_target) {
  float energy = state->energy() / 10;
  float power = state->power();
  float average_power = state->average_power();
  float normalized_power = SafeDivide(power, average_power);

  auto buffer_size = samples.size() / 2;
  vertices_.resize(buffer_size);

  auto binding_options = gl::GlTextureBindingOptions();
  binding_options.sampling_mode = gl::GlTextureSamplingMode::kClampToBorder;
  binding_options.border_color = glm::vec4(0);

  {
    auto back_activation = back_render_target_->Activate();
    waveform_program_->Use();
    glViewport(0, 0, longer_dimension(), longer_dimension());

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    wiggle_accum_ += SIGINJECT_OVERRIDE("kaleidoscope_wiggle_coeff", sin(energy) / 10, 0.0f, 2.0f * power);

    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < buffer_size; ++i) {
        float c3 = cos(wiggle_accum_ * 10 + power * 2);
        float s3 = sin(wiggle_accum_ * 10 + power * 2);
        float x_int = samples[i * 2] * kScaleFactor;
        float y_int = samples[i * 2 + 1] * kScaleFactor;

        float x_pos = x_int * c3 - y_int * s3;
        float y_pos = x_int * s3 + y_int * c3;

        x_pos += cos(sin(2 * wiggle_accum_) * wiggle_accum_ * 10 / 1.25 + power) / 5;
        x_pos += cos(sin(2 * wiggle_accum_) * wiggle_accum_ * 10 / 5.23 + 0.5) / 20;
        y_pos += sin(sin(2 * wiggle_accum_) * wiggle_accum_ * 10 / 1.25 + power) / 5;
        y_pos += sin(sin(2 * wiggle_accum_) * wiggle_accum_ * 10 / 5.23 + 0.5) / 20;

        x_pos += cos(wiggle_accum_ / 10 + (j / 4.0 * kPi * 2)) / 2;
        y_pos += sin(wiggle_accum_ / 10 + (j / 4.0 * kPi * 2)) / 2;

        vertices_[i] = glm::vec2(x_pos, y_pos);
      }

      polyline_.UpdateVertices(vertices_);
      polyline_.UpdateWidth(std::clamp(normalized_power * 10.0f, 1.0f, 5.0f));
      polyline_.UpdateColor(HsvToRgb(glm::vec3(wiggle_accum_ * 10, 1, 0.5)));
      polyline_.Draw();
    }
  }

  {
    auto front_activation = front_render_target_->Activate();

    warp_program_->Use();

    GlBindUniform(warp_program_, "last_frame_size",
                  glm::ivec2(width(), height()));
    GlBindRenderTargetTextureToUniform(warp_program_, "last_frame",
                                       back_render_target_, binding_options);

    float blur_distance = SIGINJECT_OVERRIDE(
        "kaleidoscope_blur_distance", 0.1f * sin(energy * 3), -0.1f, 0.1f);
    float warp_zoom_coeff =
        SIGINJECT_OVERRIDE("kaleidoscope_warp_zoom_coeff",
                           1.0f + 0.5f * sin(energy * 5), 0.95f, 1.05f);
    float warp_rot_coeff = SIGINJECT_OVERRIDE(
        "kaleidoscope_warp_rot_coeff", 0.1f * sin(energy * 5), -0.1f, 0.1f);
    float sample_rot_coeff =
        (sample_rot_coeff_accum_ += SIGINJECT_OVERRIDE(
             "kaleidoscope_sample_rot_coeff", kPi * 2.0f * sin(energy * 5),
             -kPi / 5.0f, kPi / 5.0f));
    float sample_scale_coeff = SIGINJECT_OVERRIDE(
        "kaleidoscope_sample_scale_coeff", 0.5f * sin(energy * 5), -0.3f, 0.3f);
    float color_coeff = SIGINJECT_OVERRIDE(
        "kaleidoscope_color_coeff", 1.0f + 0.1f * sin(energy * 10), 0.3f, 1.2f);
    int num_divisions = SIGINJECT_OVERRIDE(
        "kaleidoscope_num_divisions",
        1.0f + (UnitarySin(energy * 20.0f) * 5.0f), 1.0f, 6.0f);

    glm::vec2 blur_offset =
        glm::vec2(1.0f, 1.0f) *
        SIGINJECT_OVERRIDE("kaleidoscope_blur", UnitarySin(energy * 5) * 0.2f,
                           0.0f, 0.3f);

    GL_BIND_LOCAL(warp_program_, blur_distance);
    GL_BIND_LOCAL(warp_program_, warp_zoom_coeff);
    GL_BIND_LOCAL(warp_program_, warp_rot_coeff);
    GL_BIND_LOCAL(warp_program_, sample_rot_coeff);
    GL_BIND_LOCAL(warp_program_, sample_scale_coeff);
    GL_BIND_LOCAL(warp_program_, color_coeff);
    GL_BIND_LOCAL(warp_program_, blur_offset);
    GL_BIND_LOCAL(warp_program_, num_divisions);

    glViewport(0, 0, longer_dimension(), longer_dimension());
    rectangle_.Draw();
  }

  {
    auto output_activation = output_render_target->Activate();
    composite_program_->Use();

    GlBindUniform(composite_program_, "render_target_size",
                  glm::ivec2(width(), height()));
    GlBindRenderTargetTextureToUniform(composite_program_, "render_target",
                                       front_render_target_, binding_options);

    SquareViewport();
    rectangle_.Draw();

    back_render_target_->swap_texture_unit(front_render_target_.get());
  }
}

}  // namespace opendrop
