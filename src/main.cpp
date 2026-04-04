#include "Engine/engine.h"
#include "Sandbox/sandbox.h"

int main(int argc, const char *argv[]) {
    Config config = {
        .appName = "Sandbox",
        .vsync = Config::VsyncEnabled(argc, argv),
        .windowWidth = 1600,
        .windowHeight = 900,
        .msaaSamples = 4
    };

    Sandbox *sandbox = new Sandbox();
    Engine *engine = new Engine(sandbox, &config);

    engine->Run();
    engine->Cleanup();

    return 0;
}
