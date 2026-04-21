#pragma once 

#include <filesystem>

#include "ECS/ecsTypes.h"
#include "Component/light.h"
#include "System/Render/renderBackend.h"
#include "Util/objLoader.h"
#include "config.h"
#include "eventHandler.h"
#include "scene.h"

namespace fs = std::filesystem;

class Engine {
public:
    Engine(Config& config); 
    void Run();
    void SwitchScene(SceneHandle scene);
    void AddScene(Scene *scene);
    void Cleanup();
    void EventCallback(Event event);
    static void EventHook(Event event, void *data);
    double GetTime();
    void CreateMesh(ECS *ecs, const fs::path& objPath, const fs::path& mtlPath, std::vector<Entity>& results);
    void CreatePointLight(ECS *ecs, const LightCreateInfo& info, Entity& result);
    void CreateSpotLight(ECS *ecs, const LightCreateInfo& info, Entity& result);
    void CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, Entity& result);

    RenderBackend *GetRenderBackend() { return m_RenderBackend; };
    struct GLFWwindow *GetWindow() { return m_Window; };
    uint64_t GetFrameNumber() { return m_RenderBackend->GetFrameNumber(); };
    glm::vec2 GetFrameMouseDelta() { return m_EventHandler.mousePos - m_EventHandler.mousePosLastFrame; };
    bool *GetKeysDown() { return m_EventHandler.keysDown; };

private:
    void CreateMaterial(const MtlData& data, Material& material);
    void CalculateTangents(Vertex& v1, Vertex& v2, Vertex& v3);

private:
    EventHandler m_EventHandler;
    struct GLFWwindow *m_Window;
    RenderBackend *m_RenderBackend;
    int32_t m_MaxScenes;
    std::vector<Scene *> m_Scenes;
    SceneHandle m_ActiveScene;
};
