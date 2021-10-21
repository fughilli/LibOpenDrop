#include "libopendrop/preset/shape_bounce/shape_bounce.h"

#define _USE_MATH_DEFINES
#include <cmath>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <glm/mat4x4.hpp>

#include "libopendrop/preset/shape_bounce/composite.fsh.h"
#include "libopendrop/preset/shape_bounce/ngon.fsh.h"
#include "libopendrop/preset/shape_bounce/passthrough.vsh.h"
#include "libopendrop/preset/shape_bounce/warp.fsh.h"
#include "libopendrop/util/coefficients.h"
#include "libopendrop/util/colors.h"
#include "libopendrop/util/gl_util.h"
#include "libopendrop/util/logging.h"
#include "libopendrop/util/math.h"
#include "libopendrop/util/status_macros.h"

namespace opendrop {

namespace {
constexpr float kScaleFactor = 0.5f;
}

ShapeBounce::ShapeBounce(
    std::shared_ptr<gl::GlProgram> warp_program,
    std::shared_ptr<gl::GlProgram> composite_program,
    std::shared_ptr<gl::GlProgram> ngon_program,
    std::shared_ptr<gl::GlRenderTarget> front_render_target,
    std::shared_ptr<gl::GlRenderTarget> back_render_target,
    std::shared_ptr<gl::GlTextureManager> texture_manager, int n)
    : Preset(texture_manager),
      warp_program_(warp_program),
      composite_program_(composite_program),
      ngon_program_(ngon_program),
      front_render_target_(front_render_target),
      back_render_target_(back_render_target),
      ngon_(n),
      bass_power_filter_({0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1},
                         0.99f) {
  constexpr float kSamplingRate = 44100.0f;
  bass_filter_ = IirBandFilter(50.0f / kSamplingRate, 40.0f / kSamplingRate,
                               IirBandFilterType::kBandpass);
}

absl::StatusOr<std::shared_ptr<Preset>> ShapeBounce::MakeShared(
    std::shared_ptr<gl::GlTextureManager> texture_manager) {
  ASSIGN_OR_RETURN(
      auto warp_program,
      gl::GlProgram::MakeShared(passthrough_vsh::Code(), warp_fsh::Code()));
  ASSIGN_OR_RETURN(auto composite_program,
                   gl::GlProgram::MakeShared(passthrough_vsh::Code(),
                                             composite_fsh::Code()));
  ASSIGN_OR_RETURN(
      auto ngon_program,
      gl::GlProgram::MakeShared(passthrough_vsh::Code(), ngon_fsh::Code()));
  ASSIGN_OR_RETURN(auto front_render_target,
                   gl::GlRenderTarget::MakeShared(0, 0, texture_manager));
  ASSIGN_OR_RETURN(auto back_render_target,
                   gl::GlRenderTarget::MakeShared(0, 0, texture_manager));

  return std::shared_ptr<ShapeBounce>(
      new ShapeBounce(warp_program, composite_program, ngon_program,
                      front_render_target, back_render_target, texture_manager,
                      static_cast<int>(Coefficients::Random<1>(3, 6)[0])));
}

void ShapeBounce::OnUpdateGeometry() {
  glViewport(0, 0, width(), height());
  if (front_render_target_ != nullptr) {
    front_render_target_->UpdateGeometry(width(), height());
  }
  if (back_render_target_ != nullptr) {
    back_render_target_->UpdateGeometry(width(), height());
  }
}

void ShapeBounce::OnDrawFrame(
    absl::Span<const float> samples, std::shared_ptr<GlobalState> state,
    float alpha, std::shared_ptr<gl::GlRenderTarget> output_render_target) {
  float energy = state->energy();
  float power = state->power();

  const float bass_power = bass_power_filter_.ProcessSample(
      bass_filter_->ComputePower(state->left_channel()));

  {
    auto back_activation = back_render_target_->Activate();

    warp_program_->Use();

    GlBindUniform(warp_program_, "power", power);
    GlBindUniform(warp_program_, "energy", energy);
    GlBindUniform(warp_program_, "last_frame_size",
                  glm::ivec2(width(), height()));
    GlBindRenderTargetTextureToUniform(warp_program_, "last_frame",
                                       front_render_target_,
                                       gl::GlTextureBindingOptions());
    GlBindUniform(warp_program_, "model_transform", glm::mat4(1.0f));

    // Force all fragments to draw with a full-screen rectangle.
    rectangle_.Draw();

    // velocity_ -= glm::vec2(0, 10) * state->dt();
    position_ += velocity_ * state->dt();
    velocity_ +=
        glm::vec2(cos(energy), sin(energy)) * power * 10.0f * state->dt();

    velocity_ = velocity_ * 0.999f;

    if ((position_.x < -1 && velocity_.x < 0) ||
        (position_.x > 1 && velocity_.x > 0)) {
      velocity_.x = -velocity_.x;
    }
    if ((position_.y < -1 && velocity_.y < 0) ||
        (position_.y > 1 && velocity_.y > 0)) {
      velocity_.y = -velocity_.y;
    }

    ngon_program_->Use();
    glm::vec3 translation(position_.x, position_.y, 0);
    const float scale = MapValue<float>(bass_power, 0, 1, 0.1, 0.5);
    glm::mat4 transform =
        glm::mat4x4(scale, 0, 0, 0,                                 // Row 1
                    0, scale, 0, 0,                                 // Row 2
                    0, 0, scale, 0,                                 // Row 3
                    translation.x, translation.y, translation.z, 1  // Row 4

        );
    glm::vec4 color = glm::vec4(HsvToRgb(glm::vec3(energy, 1, 0.5)), 1);
    GlBindUniform(ngon_program_, "model_transform", transform);
    GlBindUniform(ngon_program_, "color", color);

    ngon_.Draw();
  }

  {
    auto output_activation = output_render_target->Activate();
    composite_program_->Use();

    GlBindUniform(composite_program_, "power", power);
    GlBindUniform(composite_program_, "energy", energy);
    GlBindUniform(composite_program_, "render_target_size",
                  glm::ivec2(width(), height()));
    GlBindUniform(composite_program_, "alpha", alpha);
    GlBindRenderTargetTextureToUniform(composite_program_, "render_target",
                                       back_render_target_,
                                       gl::GlTextureBindingOptions());
    GlBindUniform(composite_program_, "model_transform", glm::mat4(1.0f));

    glViewport(0, 0, width(), height());
    rectangle_.Draw();

    back_render_target_->swap_texture_unit(front_render_target_.get());
  }
}

}  // namespace opendrop
