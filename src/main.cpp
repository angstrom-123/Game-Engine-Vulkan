#include "Engine/engine.h"

#include "Engine/Util/profiler.h"
#include "Application/SponzaScene/sponzaScene.h"
#include "Application/ApesScene/apesScene.h"

int main(int argc, const char *argv[]) {
    PROFILER_BEGIN_SESSION("Profiling_Session");

    Config config = {
        .appName = "My App",
        .vsync = Config::VsyncEnabled(argc, argv),
        .windowWidth = 1600,
        .windowHeight = 900,
        .msaaSamples = 4,
    };
    Engine engine(config);

    engine.RegisterScene<SponzaScene>("src/Application/SponzaScene");
    engine.RegisterScene<ApesScene>("src/Application/ApesScene");

    // Scene name specified in its MANIFEST.yaml under the `manifest` tag
    engine.Run("SponzaScene");

    PROFILER_END_SESSION();
}
