#pragma once 

#include <filesystem>

#include "ECS/ecsTypes.h"
#include "Component/light.h"
#include "System/Render/renderBackend.h"
#include "Util/objLoader.h"
#include "config.h"
#include "eventManager.h"
#include "scene.h"
#include "sceneManager.h"

namespace fs = std::filesystem;

class Engine {
public:
    Engine(Config& config);
    void Run();
    void Cleanup();
    void EventCallback(Event event);
    static void EventHook(Event event, void *data);
    void CreateMesh(ECS *ecs, const fs::path& objPath, const fs::path& mtlPath, std::vector<Entity>& results);
    void CreatePointLight(ECS *ecs, const LightCreateInfo& info, Entity& result);
    void CreateSpotLight(ECS *ecs, const LightCreateInfo& info, Entity& result);
    void CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, Entity& result);

    double GetTime() { return glfwGetTime(); }
    uint64_t GetFrameNumber() { return renderBackend->GetFrameNumber(); }
    glm::vec2 GetFrameMouseDelta() { return eventManager.mousePos - eventManager.mousePosLastFrame; }
    bool *GetKeysDown() { return eventManager.keysDown; }

public:
    struct GLFWwindow *window;
    RenderBackend *renderBackend;

    EventManager eventManager;
    SceneManager sceneManager;

private:
    void CreateMaterial(const MtlData& data, Material& material);
    void CalculateTangents(Vertex& v1, Vertex& v2, Vertex& v3);
};
