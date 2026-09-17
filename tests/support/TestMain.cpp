// Custom Catch2 entry point so the shared GpuEnv lives on main()'s stack.
// Destroying the VkDevice/VkInstance during static destruction raced with the
// Vulkan loader's and validation layers' own atexit teardown (abort or hang).

#include "TestEnv.hpp"

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[])
{
  // On failure GPU-backed tests SKIP(); the session still runs.
  const bool gpuAvailable = vdtest::InitGpuEnv();
  const auto requireGpu = std::getenv("VD_TEST_REQUIRE_GPU");
  if(!gpuAvailable && requireGpu && requireGpu[0] != '\0' &&
     std::strcmp(requireGpu, "0") != 0) {
    std::fprintf(
      stderr,
      "[vuldir-tests] VD_TEST_REQUIRE_GPU is set, but GPU environment "
      "initialization failed.\n");
    vdtest::ShutdownGpuEnv();
    return 2;
  }

  Catch::Session session;

  const int cliResult = session.applyCommandLine(argc, argv);
  if(cliResult != 0) {
    vdtest::ShutdownGpuEnv();
    return cliResult;
  }

  const int result = session.run();

  vdtest::ShutdownGpuEnv();

  return result;
}
