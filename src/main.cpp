#include "Engine/engine.h"
#include "Application/sandboxScene.h"
#include "Application/apesScene.h"

#include "Engine/Util/profiler.h"

int main(int argc, const char *argv[]) {
    PROFILER_BEGIN_SESSION("Profiling_Session");

    // Engine
    Config config = {
        .appName = "My App",
        .vsync = Config::VsyncEnabled(argc, argv),
        .windowWidth = 1600,
        .windowHeight = 900,
        .msaaSamples = 4,
        .maxScenes = 10
    };
    Engine engine(config);

    // Scenes
    Scene sandbox = engine.sceneManager.RegisterScene<Sandbox>("sandbox");
    Scene apes = engine.sceneManager.RegisterScene<Apes>("apes");

    // Run
    engine.Run(sandbox);

    PROFILER_END_SESSION();
}
