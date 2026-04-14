#include "Engine/engine.h"
#include "Sandbox/sandbox.h"
#include "Engine/Util/profiler.h"

int main(int argc, const char *argv[]) {
    Config config = {
        .appName = "Sandbox",
        .vsync = Config::VsyncEnabled(argc, argv),
        .windowWidth = 1600,
        .windowHeight = 900,
        .msaaSamples = 4
    };

    PROFILER_BEGIN_SESSION("Profiling_Session");

    Sandbox sandbox;
    Engine engine(&sandbox, config);

    engine.Run();
    engine.Cleanup();

    PROFILER_END_SESSION();

    return 0;
}
