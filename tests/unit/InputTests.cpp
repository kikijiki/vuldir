#include "Input.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vd;

TEST_CASE("mouse motion deltas use Cartesian signs", "[sample][input]")
{
  sample::MouseDeltaTracker tracker;

  auto delta = tracker.Update(10, 20);
  REQUIRE(delta[0] == 0);
  REQUIRE(delta[1] == 0);

  delta = tracker.Update(14, 17);
  REQUIRE(delta[0] == 4);
  REQUIRE(delta[1] == 3);

  delta = tracker.Update(11, 22);
  REQUIRE(delta[0] == -3);
  REQUIRE(delta[1] == -5);
}
