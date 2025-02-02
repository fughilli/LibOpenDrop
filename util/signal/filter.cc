#include "util/signal/filter.h"

#include <cmath>

#include "util/math/math.h"

namespace opendrop {

float Filter::ComputePower(absl::Span<const float> samples) {
  float power = 0.0f;
  for (auto sample : samples) {
    float output_sample = ProcessSample(sample);
    power += output_sample * output_sample;
  }
  return power / samples.size();
}

FirFilter::FirFilter(std::initializer_list<float> taps) : taps_(taps) {
  input_history_.resize(taps.size(), 0);
}

float FirFilter::ProcessSample(float sample) {
  std::copy_backward(input_history_.begin(), std::prev(input_history_.end()),
                     input_history_.end());
  input_history_[0] = sample;

  float output = 0.0f;
  for (int i = 0; i < input_history_.size(); ++i) {
    output += input_history_[i] * taps_[i];
  }

  return output;
}

IirFilter::IirFilter(std::initializer_list<float> x_taps,
                     std::initializer_list<float> y_taps)
    : x_taps_(x_taps), y_taps_(y_taps) {
  input_history_.resize(x_taps.size(), 0);
  output_history_.resize(y_taps.size(), 0);
}

float IirFilter::ProcessSample(float sample) {
  std::copy_backward(input_history_.begin(), std::prev(input_history_.end()),
                     input_history_.end());
  input_history_[0] = sample;

  float output = 0.0f;
  for (int i = 0; i < input_history_.size(); ++i) {
    output += input_history_[i] * x_taps_[i];
  }

  for (int i = 0; i < output_history_.size(); ++i) {
    output += output_history_[i] * y_taps_[i];
  }

  std::copy_backward(output_history_.begin(), std::prev(output_history_.end()),
                     output_history_.end());
  output_history_[0] = output;

  return output;
}

std::shared_ptr<IirFilter> IirBandFilter(float center_frequency,
                                         float bandwidth,
                                         IirBandFilterType type) {
  float cos_2_pi_f = cos(2.0f * M_PI * center_frequency);
  float R = 1.0f - 3.0f * bandwidth;
  float K =
      (1.0f - 2.0f * R * cos_2_pi_f + (R * R)) / (2.0f - 2.0f * cos_2_pi_f);

  switch (type) {
    case IirBandFilterType::kBandpass:
      return std::make_unique<IirFilter>(std::initializer_list<float>({
                                             1.0f - K,                     // a0
                                             2.0f * (K - R) * cos_2_pi_f,  // a1
                                             (R * R) - K                   // a2
                                         }),
                                         std::initializer_list<float>({
                                             2.0f * R * cos_2_pi_f,  // b1
                                             -(R * R)                // b2
                                         }));
    case IirBandFilterType::kBandstop:
      return std::make_unique<IirFilter>(std::initializer_list<float>({
                                             K,                       // a0
                                             -2.0f * K * cos_2_pi_f,  // a1
                                             K                        // a2
                                         }),
                                         std::initializer_list<float>({
                                             2.0f * R * cos_2_pi_f,  // b1
                                             -(R * R)                // b2
                                         }));
  }
}

std::shared_ptr<IirFilter> IirSinglePoleFilter(float cutoff_frequency,
                                               IirSinglePoleFilterType type) {
  const float pi_2_fc = 2.0f * M_PI * cutoff_frequency;
  const float decay = pi_2_fc / (pi_2_fc + 1.0f);
  switch (type) {
    case IirSinglePoleFilterType::kLowpass:
      return std::make_unique<IirFilter>(std::initializer_list<float>({
                                             decay,
                                         }),
                                         std::initializer_list<float>({
                                             1 - decay,
                                         }));
    case IirSinglePoleFilterType::kHighpass:
      return std::make_unique<IirFilter>(std::initializer_list<float>({
                                             1 - decay,
                                         }),
                                         std::initializer_list<float>({
                                             decay,
                                         }));
  }
}

float HystereticMapFilter::ProcessSample(float sample) {
  const float average = averaging_filter_->ProcessSample(sample);
  if (sample > maximum_) {
    maximum_ = sample;
  } else {
    maximum_ = maximum_ * alpha_ + average * (1.0f - alpha_);
  }
  if (sample < minimum_) {
    minimum_ = sample;
  } else {
    minimum_ = minimum_ * alpha_ + average * (1.0f - alpha_);
  }

  if (maximum_ == minimum_) {
    return sample;
  }

  return MapValue<float, true>(sample, minimum_, maximum_, 0.0f, 1.0f);
}

}  // namespace opendrop
