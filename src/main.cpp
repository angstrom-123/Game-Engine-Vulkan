#include "Engine/engine.h"

#include "Application/SponzaScene/sponzaScene.h"
#include "Application/ApesScene/apesScene.h"
#include "Application/ShadowTestScene/shadowTestScene.h"

int main() 
{
    Engine engine;

    engine.RegisterScene<SponzaScene>("src/Application/SponzaScene");
    engine.RegisterScene<ApesScene>("src/Application/ApesScene");
    engine.RegisterScene<ShadowTestScene>("src/Application/ShadowTestScene");

    engine.Run();
}
