#include "preset/graph_preset/graph_preset.h"

#include <algorithm>
#include <cmath>

#include "preset/graph_preset/model_frag.fsh.h"
#include "preset/graph_preset/passthrough_vert.vsh.h"
#include "preset/graph_preset/zoom_frag.fsh.h"
#include "third_party/gl_helper.h"
#include "third_party/glm_helper.h"
#include "util/graph/graph.h"
#include "util/graph/rendering.h"
#include "util/graph/types/color.h"
#include "util/graph/types/monotonic.h"
#include "util/graph/types/texture.h"
#include "util/graph/types/types.h"
#include "util/graph/types/unitary.h"
#include "util/graphics/colors.h"
#include "util/graphics/gl_util.h"
#include "util/logging/logging.h"
#include "util/math/vector.h"
#include "util/status/status_macros.h"

namespace opendrop {

namespace {
constexpr float kScaleFactor = 0.5f;

std::shared_ptr<gl::GlProgram> model;
std::shared_ptr<gl::GlProgram> zoom;

absl::Status InitGlPrograms() {
  if (model == nullptr) {
    ASSIGN_OR_RETURN(model,
                     gl::GlProgram::MakeShared(passthrough_vert_vsh::Code(),
                                               model_frag_fsh::Code()));
  }
  if (zoom == nullptr) {
    ASSIGN_OR_RETURN(zoom,
                     gl::GlProgram::MakeShared(passthrough_vert_vsh::Code(),
                                               zoom_frag_fsh::Code()));
  }
  return absl::OkStatus();
}

}  // namespace

GraphPreset::GraphPreset(std::shared_ptr<gl::GlTextureManager> texture_manager)
    : Preset(texture_manager) {
  // Configure graph.
  graph_builder_.DeclareConversion<std::tuple<Monotonic>, std::tuple<Unitary>>(
      "sinusoid", [](std::tuple<Monotonic> in) -> std::tuple<Unitary> {
        return std::tuple<Unitary>(
            Unitary((1.0f + std::cos(std::get<0>(in))) / 2.0f));
      });
  graph_builder_.DeclareConversion<std::tuple<Unitary>, std::tuple<Color>>(
      "color_wheel", [](std::tuple<Unitary> in) -> std::tuple<Color> {
        glm::vec4 color =
            glm::vec4(HsvToRgb(glm::vec3(std::get<0>(in), 1.0f, 1.0f)), 1.0f);
        return std::tuple<Color>(color);
      });
  graph_builder_.DeclareConversion<std::tuple<Color>, std::tuple<Texture>>(
      "colored_rectangle",
      [this, texture_manager](std::tuple<Color> in) -> std::tuple<Texture> {
        Texture tex(width(), height(), texture_manager);

        auto rt_activation = tex.RenderTarget()->Activate();
        auto shader_activation = model->Activate();

        glm::vec4 color = std::get<0>(in);

        GlBindUniform(model, "model_transform", ScaleTransform(1.0f));

        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        Rectangle rectangle;
        rectangle.SetColor(color);
        rectangle.Draw();

        return std::make_tuple(tex);
      });

  graph_builder_
      .DeclareConversion<std::tuple<Texture, Texture>, std::tuple<Texture>>(
          "zoom",
          [this, texture_manager](
              std::tuple<Texture, Texture> in) -> std::tuple<Texture> {
            auto& [in_tex_a, in_tex_b] = in;
            Texture tex(width(), height(), texture_manager);

            auto rt_activation = tex.RenderTarget()->Activate();
            auto shader_activation = zoom->Activate();

            GlBindUniform(zoom, "model_transform", glm::mat4(1.0f));
            GlBindRenderTargetTextureToUniform(zoom, "in_tex_a",
                                               in_tex_a.RenderTarget(),
                                               gl::GlTextureBindingOptions());
            GlBindRenderTargetTextureToUniform(zoom, "in_tex_b",
                                               in_tex_b.RenderTarget(),
                                               gl::GlTextureBindingOptions());

            Rectangle().Draw();

            return std::make_tuple(tex);
          });

  evaluation_graph_ =
      graph_builder_
          .Bridge(ConstructTypes<Monotonic>(), ConstructTypes<Texture>())
          .value();

  ax::NodeEditor::Config config{};
  config.SettingsFile = "graph_nodes.json";
  editor_context_ = ax::NodeEditor::CreateEditor(&config);
}

GraphPreset::~GraphPreset() { ax::NodeEditor::DestroyEditor(editor_context_); }

absl::StatusOr<std::shared_ptr<Preset>> GraphPreset::MakeShared(
    std::shared_ptr<gl::GlTextureManager> texture_manager) {
  RETURN_IF_ERROR(InitGlPrograms());
  return std::shared_ptr<GraphPreset>(new GraphPreset(texture_manager));
}

void GraphPreset::OnUpdateGeometry() { glViewport(0, 0, width(), height()); }

void GraphPreset::OnDrawFrame(
    absl::Span<const float> samples, std::shared_ptr<GlobalState> state,
    float alpha, std::shared_ptr<gl::GlRenderTarget> output_render_target) {
  ImGui::Begin("Graph Viewer", nullptr, ImGuiWindowFlags_NoScrollbar);
  if (ImGui::Button("Again"))
    evaluation_graph_ =
        graph_builder_
            .Bridge(ConstructTypes<Monotonic>(), ConstructTypes<Texture>())
            .value();
  RenderGraph(editor_context_, evaluation_graph_);
  ImGui::End();

  evaluation_graph_.Evaluate(std::tuple<Monotonic>(state->energy()));
  Texture tex = std::get<0>(evaluation_graph_.Result<Texture>());

  {
    auto output_activation = output_render_target->Activate();
    Blit(tex);
  }
}

}  // namespace opendrop
