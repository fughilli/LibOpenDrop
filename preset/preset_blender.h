#ifndef PRESET_PRESET_BLENDER_H_
#define PRESET_PRESET_BLENDER_H_

#include <list>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "preset/preset.h"
#include "primitive/rectangle.h"
#include "util/logging/logging.h"
#include "util/time/oneshot.h"

namespace opendrop {
enum PresetActivationState {
  kTransitionIn = 0,
  kIn,
  kAwaitingTransitionOut,
  kTransitionOut,
  kOut
};

class PresetActivation {
 public:
  PresetActivation(std::shared_ptr<Preset> preset,
                   std::shared_ptr<gl::GlRenderTarget> render_target,
                   float minimum_duration_s, float transition_duration_s);

  PresetActivationState Update(float dt);

  void TriggerTransitionOut();

  float GetMixingCoefficient() const;

  std::shared_ptr<Preset> preset() { return preset_; }
  std::shared_ptr<gl::GlRenderTarget> render_target() { return render_target_; }

  PresetActivationState state() const { return state_; }

 private:
  std::shared_ptr<Preset> preset_;
  std::shared_ptr<gl::GlRenderTarget> render_target_;
  PresetActivationState state_;
  OneshotIncremental<float> expiry_timer_, transition_timer_;

  float maximal_mix_coeff_ = 1.0f;
};

class PresetBlender {
 public:
  PresetBlender(int width, int height);

  template <typename... Args>
  void AddPreset(Args&&... args) {
    if (preset_activations_.size() == 1) {
      auto& activation = preset_activations_.front();
      auto preset = activation.preset();
      if (preset->should_solo() &&
          activation.state() < PresetActivationState::kAwaitingTransitionOut) {
        LOG(INFO) << "Current preset is soloing...";
        return;
      }
    }
    LOG(INFO) << "Adding preset";
    PresetActivation activation(std::forward<Args>(args)...);
    if (activation.preset()->should_solo()) {
      for (auto& activation : preset_activations_) {
        activation.TriggerTransitionOut();
      }
    }
    activation.preset()->UpdateGeometry(width_, height_);
    activation.render_target()->UpdateGeometry(width_, height_);
    preset_activations_.emplace_back(std::move(activation));
  }

  // Draws a single frame of blended preset output.
  void DrawFrame(absl::Span<const float> samples,
                 std::shared_ptr<GlobalState> state,
                 std::shared_ptr<gl::GlRenderTarget> output_render_target);

  void UpdateGeometry(int width, int height);

  size_t NumPresets() const { return preset_activations_.size(); }

  int QueryPresetCount(std::string_view name);

  void TransitionOutAll();

 private:
  void Update(float dt);

  int width_, height_;
  std::shared_ptr<gl::GlProgram> blit_program_;
  std::list<PresetActivation> preset_activations_;

  Rectangle rectangle_;
};
}  // namespace opendrop

#endif  // PRESET_PRESET_BLENDER_H_
