#include "Engine/engine.h"
#include "Sandbox/sandbox.h"

#include "Engine/Util/profiler.h"

int main(int argc, const char *argv[]) {
    PROFILER_BEGIN_SESSION("Profiling_Session");

    // Engine setup
    Config config = {
        .appName = "My App",
        .vsync = Config::VsyncEnabled(argc, argv),
        .windowWidth = 1600,
        .windowHeight = 900,
        .msaaSamples = 4,
        .maxScenes = 10
    };
    Engine engine(config);

    // Sandbox scene setup
    SandboxUserData sandboxConfig = {
        .windowWidth = config.windowWidth,
        .windowHeight = config.windowHeight
    };
    Scene sandbox = engine.sceneManager.RegisterScene<Sandbox>();
    engine.sceneManager.LoadScene(&engine, sandbox, &sandboxConfig);
    engine.sceneManager.SwitchScene(sandbox);

    // Start running
    engine.Run();
    engine.Cleanup();

    PROFILER_END_SESSION();

    return 0;
}
