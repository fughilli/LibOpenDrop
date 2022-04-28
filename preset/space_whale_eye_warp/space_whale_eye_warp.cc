#include "preset/space_whale_eye_warp/space_whale_eye_warp.h"

#include <algorithm>
#include <cmath>

#include "debug/control_injector.h"
#include "debug/signal_scope.h"
#include "preset/common/outline_model.h"
#include "preset/space_whale_eye_warp/composite.fsh.h"
#include "preset/space_whale_eye_warp/passthrough_frag.fsh.h"
#include "preset/space_whale_eye_warp/passthrough_vert.vsh.h"
#include "preset/space_whale_eye_warp/warp.fsh.h"
#include "third_party/gl_helper.h"
#include "third_party/glm_helper.h"
#include "util/enums.h"
#include "util/graphics/colors.h"
#include "util/graphics/gl_util.h"
#include "util/logging/logging.h"
#include "util/math/math.h"
#include "util/math/perspective.h"
#include "util/math/vector.h"
#include "util/signal/signals.h"
#include "util/status/status_macros.h"

namespace opendrop {

namespace {
constexpr float kScaleFactor = 2.0f;
}  // namespace

SpaceWhaleEyeWarp::SpaceWhaleEyeWarp(
    std::shared_ptr<gl::GlProgram> warp_program,
    std::shared_ptr<gl::GlProgram> composite_program,
    std::shared_ptr<gl::GlProgram> passthrough_program,
    std::shared_ptr<gl::GlRenderTarget> model_texture_target,
    std::shared_ptr<gl::GlRenderTarget> front_render_target,
    std::shared_ptr<gl::GlRenderTarget> back_render_target,
    std::shared_ptr<gl::GlRenderTarget> depth_output_target,
    std::shared_ptr<OutlineModel> outline_model,
    std::shared_ptr<gl::GlTextureManager> texture_manager)
    : Preset(texture_manager),
      warp_program_(warp_program),
      composite_program_(composite_program),
      passthrough_program_(passthrough_program),
      model_texture_target_(model_texture_target),
      front_render_target_(front_render_target),
      back_render_target_(back_render_target),
      depth_output_target_(depth_output_target),
      outline_model_(outline_model)

{}

absl::StatusOr<std::shared_ptr<Preset>> SpaceWhaleEyeWarp::MakeShared(
    std::shared_ptr<gl::GlTextureManager> texture_manager) {
  ASSIGN_OR_RETURN(auto warp_program,
                   gl::GlProgram::MakeShared(passthrough_vert_vsh::Code(),
                                             warp_fsh::Code()));
  ASSIGN_OR_RETURN(auto composite_program,
                   gl::GlProgram::MakeShared(passthrough_vert_vsh::Code(),
                                             composite_fsh::Code()));
  ASSIGN_OR_RETURN(auto passthrough_program,
                   gl::GlProgram::MakeShared(passthrough_vert_vsh::Code(),
                                             passthrough_frag_fsh::Code()));
  ASSIGN_OR_RETURN(auto front_render_target,
                   gl::GlRenderTarget::MakeShared(0, 0, texture_manager));
  ASSIGN_OR_RETURN(auto back_render_target,
                   gl::GlRenderTarget::MakeShared(0, 0, texture_manager));
  ASSIGN_OR_RETURN(auto depth_output_target,
                   gl::GlRenderTarget::MakeShared(0, 0, texture_manager,
                                                  {.enable_depth = true}));
  ASSIGN_OR_RETURN(auto outline_model, OutlineModel::MakeShared());

  return std::shared_ptr<SpaceWhaleEyeWarp>(new SpaceWhaleEyeWarp(
      warp_program, composite_program, passthrough_program, nullptr,
      front_render_target, back_render_target, depth_output_target,
      outline_model, texture_manager));
}

void SpaceWhaleEyeWarp::OnUpdateGeometry() {
  glViewport(0, 0, width(), height());
  if (model_texture_target_ != nullptr) {
    model_texture_target_->UpdateGeometry(longer_dimension(),
                                          longer_dimension());
  }
  if (front_render_target_ != nullptr) {
    front_render_target_->UpdateGeometry(longer_dimension(),
                                         longer_dimension());
  }
  if (back_render_target_ != nullptr) {
    back_render_target_->UpdateGeometry(longer_dimension(), longer_dimension());
  }
  if (depth_output_target_ != nullptr) {
    depth_output_target_->UpdateGeometry(longer_dimension(),
                                         longer_dimension());
  }
}

std::tuple<int, float> CountAndScale(float arg, int max_count) {
  int n_clusters = 1.0f + std::fmod(arg, max_count);
  float cluster_scale = (cos(arg * 2.0f * kPi) + 1.0f) / 2.0f;
  return std::make_tuple(n_clusters, cluster_scale);
}
float EstimateBeatPhase(GlobalState& state) { return 0; }

void SpaceWhaleEyeWarp::DrawEyeball(GlobalState& state, glm::vec2 zoom_vec) {
  float eye_scale = SIGPLOT(
      "eye_scale", 0.4 + SineEase(beat_estimators_[0].triangle_phase()) * 0.2);
  glm::mat4 model_transform =
      ScaleTransform(eye_scale) *
      glm::mat4(
          OrientTowards(glm::vec3(zoom_vec, 0) + Directions::kIntoScreen)) *
      RotateAround(Directions::kUp, kPi / 2);
  const glm::vec4 color_a =
      glm::vec4(HsvToRgb(glm::vec3(state.energy(), 1, 1)), 1);
  const glm::vec4 color_b =
      glm::vec4(HsvToRgb(glm::vec3(state.energy() + 0.5, 1, 1)), 1);
  outline_model_->Draw({
      .model_transform = model_transform,
      .color_a = color_a,
      .color_b = color_b,
      .render_target = back_render_target_,
      .alpha = 1,
      .energy = state.energy(),
      .blend_coeff = texture_trigger_ ? 0.3f : 0.0f,
      .model_to_draw = OutlineModel::ModelToDraw::kEyeball,
      .pupil_size = 0.5f + (1.0f + beat_estimators_[0].triangle_phase()) / 2.0f,
  });
}

void SpaceWhaleEyeWarp::OnDrawFrame(
    absl::Span<const float> samples, std::shared_ptr<GlobalState> state,
    float alpha, std::shared_ptr<gl::GlRenderTarget> output_render_target) {
  for (int i = 0; i < 3; ++i) {
    beat_estimators_[i].Estimate(state->channel_band(i), state->dt());
  }

  float zoom_coeff = 1.1f;

  glm::vec2 zoom_vec = UnitVectorAtAngle(zoom_angle_);

  zoom_angle_ += (0.3 + (beat_estimators_[0].triangle_phase() *
                         sin(state->energy() * 10))) /
                 10;

  {
    auto depth_output_activation = depth_output_target_->Activate();
    glViewport(0, 0, longer_dimension(), longer_dimension());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthRange(0, 10);
    glEnable(GL_DEPTH_TEST);

    DrawEyeball(*state, zoom_vec);

    glDisable(GL_DEPTH_TEST);
  }

  {
    auto front_activation = front_render_target_->Activate();

    auto program_activation = warp_program_->Activate();

    GlBindUniform(warp_program_, "frame_size", glm::ivec2(width(), height()));
    GlBindUniform(warp_program_, "power", state->power());
    GlBindUniform(warp_program_, "energy", state->energy());
    // Figure out how to keep it from zooming towards the viewer when the line
    // is moving
    GlBindUniform(warp_program_, "zoom_coeff", zoom_coeff);
    GlBindUniform(warp_program_, "zoom_vec", zoom_vec);
    GlBindUniform(warp_program_, "model_transform", glm::mat4(1.0f));
    auto binding_options = gl::GlTextureBindingOptions();
    background_hue_ +=
        state->power() *
        SIGINJECT_OVERRIDE("space_whale_eye_warp_border_hue_coeff", 0.1f, 0.0f,
                           3.0f);
    binding_options.border_color = glm::vec4(
        HsvToRgb(glm::vec3(
            background_hue_, 1,
            SIGINJECT_OVERRIDE("space_whale_eye_warp_border_value_coeff", 1.0f,
                               0.0f, 1.0f))),
        1);
    binding_options.sampling_mode = gl::GlTextureSamplingMode::kClampToBorder;
    GlBindRenderTargetTextureToUniform(warp_program_, "last_frame",
                                       back_render_target_, binding_options);
    GlBindRenderTargetTextureToUniform(warp_program_, "input",
                                       depth_output_target_, binding_options);

    glViewport(0, 0, longer_dimension(), longer_dimension());
    rectangle_.Draw();
  }

  {
    auto output_activation = output_render_target->Activate();
    auto program_activation = composite_program_->Activate();

    GlBindUniform(composite_program_, "render_target_size",
                  glm::ivec2(width(), height()));
    GlBindUniform(composite_program_, "model_transform", glm::mat4(1.0f));
    GlBindRenderTargetTextureToUniform(composite_program_, "render_target",
                                       front_render_target_,
                                       gl::GlTextureBindingOptions());

    SquareViewport();
    rectangle_.Draw();

    back_render_target_->swap_texture_unit(front_render_target_.get());
  }
}

}  // namespace opendrop
