#include "Engine/engine.h"
#include "Sandbox/sandbox.h"

int main(int argc, const char *argv[]) {
    Config config = Config(argc, argv);
    config.appName = "Sandbox";

    Sandbox *sandbox = new Sandbox();
    Engine *engine = new Engine(sandbox, &config);

    engine->Run();
    engine->Cleanup();

    return 0;
}
