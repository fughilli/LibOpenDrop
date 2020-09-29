#include "libopendrop/preset/preset.h"

namespace opendrop {

void Preset::DrawFrame(
    absl::Span<const float> samples, std::shared_ptr<GlobalState> state,
    std::shared_ptr<gl::GlRenderTarget> output_render_target) {
  std::unique_lock<std::mutex> lock(state_mu_);
  OnDrawFrame(samples, state, output_render_target);
}

void Preset::UpdateGeometry(int width, int height) {
  std::unique_lock<std::mutex> lock(state_mu_);
  width_ = width;
  height_ = height;
  OnUpdateGeometry();
}

}  // namespace opendrop
