#include "util/graph/graph.h"

#include <cmath>
#include <tuple>

#include "googlemock/include/gmock/gmock-matchers.h"
#include "googlemock/include/gmock/gmock-more-matchers.h"
#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "third_party/gl_helper.h"
#include "util/graph/graph_builtins.h"
#include "util/graphics/colors.h"
#include "util/graphics/gl_util.h"
#include "util/math/math.h"
#include "util/testing/graph_matchers.h"

namespace opendrop {
namespace {

using ::opendrop::graph_testing::TextureIsNear;

TEST(GraphTest, ToTypeTest) {
  EXPECT_EQ(ToType<float>(), Type::kFloatGeneric);
  EXPECT_EQ(ToType<Unitary>(), Type::kUnitary);
  EXPECT_EQ(ToType<Monotonic>(), Type::kMonotonic);
}

TEST(GraphTest, ConstructTypesTest) {
  EXPECT_THAT(ConstructTypesFromTuple(std::tuple<>()), ::testing::IsEmpty());
  EXPECT_THAT(ConstructTypesFromTuple(std::tuple<Monotonic, Unitary>()),
              ::testing::ElementsAre(Type::kMonotonic, Type::kUnitary));
  EXPECT_THAT(
      ConstructTypesFromTuple(std::tuple<Unitary, Monotonic, Unitary>()),
      ::testing::ElementsAre(Type::kUnitary, Type::kMonotonic, Type::kUnitary));
}

TEST(GraphTest, CanConstructConversion) {
  std::function<std::tuple<float>(std::tuple<float>)> convert_fn =
      [](std::tuple<float> in) -> std::tuple<float> {
    return std::make_tuple(std::get<0>(in));
  };
  auto conversion = std::make_shared<Conversion>("foobar", convert_fn);
}

TEST(GraphTest, CanInvokeConversion) {
  std::function<std::tuple<float>(std::tuple<float>)> convert_fn =
      [](std::tuple<float> in) -> std::tuple<float> {
    return std::make_tuple(std::get<0>(in) * 2);
  };
  auto conversion = std::make_shared<Conversion>("times2", convert_fn);

  EXPECT_NEAR(std::get<0>(conversion->Invoke(std::tuple<float>(123.123f))
                              .Result<std::tuple<float>>()),
              246.246f, 1e-6f);
}

TEST(GraphTest, SimpleConversion) {
  ComputeGraph graph;
  graph.DeclareConversion<std::tuple<Monotonic>, std::tuple<Unitary>>(
      "sinusoid", [](std::tuple<Monotonic> in) -> std::tuple<Unitary> {
        return std::tuple<Unitary>(
            Unitary((1.0f + std::sin(std::get<0>(in))) / 2.0f));
        return std::tuple<Unitary>(Unitary(0));
      });

  graph.Construct("sinusoid");

  std::tuple<Unitary> out;
  graph.Evaluate(std::make_tuple<Monotonic>(0), out);
  EXPECT_NEAR(std::get<0>(out), 0, 1e-6f);
  EXPECT_NEAR(std::get<0>(graph.Evaluate<std::tuple<Unitary>>(
                  std::tuple<Monotonic>(kPi / 2))),
              1.0f, 1e-6f);
}

TEST(GraphTest, ComplexConversion) {
  ComputeGraph graph;
  graph.DeclareConversion<std::tuple<Monotonic>, std::tuple<Unitary>>(
      "sinusoid", [](std::tuple<Monotonic> in) -> std::tuple<Unitary> {
        return std::tuple<Unitary>(
            Unitary((1.0f + std::sin(std::get<0>(in))) / 2.0f));
        return std::tuple<Unitary>(Unitary(0));
      });
  graph.DeclareConversion<std::tuple<Unitary>, std::tuple<Texture>>(
      "texture_all_one_color",
      [](std::tuple<Unitary> in) -> std::tuple<Texture> {
        Texture t(0);
        {
          auto activation = t.ActivateRenderContext();
          glm::vec4 color =
              glm::vec4(HsvToRgb(glm::vec3(std::get<0>(in))), 1.0f);
          gl::GlClear(color);
        }
        return std::make_tuple(t);
      });

  graph.DeclareConversion<std::tuple<Texture>, std::tuple<int>>(
      "texture_to_screen", [](std::tuple<Texture> in) -> std::tuple<int> {
        DrawTextureToScreen(std::get<0>(in));
        return std::tuple<int>(0);
      });

  // std::tuple<Texture> out;
  // graph.OrganizeAndEvaluate(std::make_tuple<Monotonic>(0), out);

  // EXPECT_THAT(std::get<0>(out), TextureIsNear(Texture(0)));
  // EXPECT_THAT(std::get<0>(graph.Evaluate<std::tuple<Unitary>>(
  //                 std::tuple<Monotonic>(kPi / 2))),
  //             TextureIsNear(Texture(1.0f)));
}

}  // namespace
}  // namespace opendrop
