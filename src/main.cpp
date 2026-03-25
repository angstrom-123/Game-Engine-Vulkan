#include "vk_engine.h"

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    Config config = {
        // .syncStrategy = SYNC_STRATEGY_VSYNC,
        .syncStrategy = SYNC_STRATEGY_UNCAPPED,
    };

    VulkanEngine engine;
    engine.Init(&config);
    engine.Run();
    engine.Cleanup();

    return 0;
}
