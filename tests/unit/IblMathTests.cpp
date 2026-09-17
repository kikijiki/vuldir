#include "IblMath.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vd;

TEST_CASE(
  "equirectangular coordinates store positive Y at the top",
  "[sample][ibl]")
{
  const auto up   = sample::DirectionToEquirectUV({0.f, 1.f, 0.f});
  const auto down = sample::DirectionToEquirectUV({0.f, -1.f, 0.f});

  REQUIRE(up[1] == Catch::Approx(0.f));
  REQUIRE(down[1] == Catch::Approx(1.f));
}

TEST_CASE(
  "equirectangular direction conversion round trips", "[sample][ibl]")
{
  const Float3 directions[] = {
    {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},       {0.f, -1.f, 0.f},
    {0.f, 0.f, 1.f}, {-0.25f, 0.75f, 0.5f},
  };

  for(const auto& direction: directions) {
    const auto expected = mt::Norm(direction);
    const auto actual   = sample::EquirectUVToDirection(
      sample::DirectionToEquirectUV(direction));
    for(u32 axis = 0u; axis < 3u; ++axis)
      REQUIRE(
        actual[axis] == Catch::Approx(expected[axis]).margin(1e-5f));
  }
}

TEST_CASE("IBL camera rays follow the view basis", "[sample][ibl]")
{
  const Float3 cameraPosition{0.f, 0.f, 2.f};
  const Float3 cameraTarget{};

  const auto center = sample::CameraRay(
    cameraPosition, cameraTarget, {0.f, 0.f}, 1.f, 1.f);
  REQUIRE(center[0] == Catch::Approx(0.f));
  REQUIRE(center[1] == Catch::Approx(0.f));
  REQUIRE(center[2] == Catch::Approx(-1.f));

  const auto positiveX = sample::CameraRay(
    cameraPosition, cameraTarget, {1.f, 0.f}, 1.f, 1.f);
  REQUIRE(positiveX[0] < 0.f);
  REQUIRE(positiveX[1] == Catch::Approx(0.f));

  const auto positiveY = sample::CameraRay(
    cameraPosition, cameraTarget, {0.f, 1.f}, 1.f, 1.f);
  REQUIRE(positiveY[0] == Catch::Approx(0.f));
  REQUIRE(positiveY[1] > 0.f);
}
