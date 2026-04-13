#pragma once 

#include <filesystem>

#include "System/Render/lightSystem.h"
#include "System/Render/renderSystem.h"
#include "Util/objLoader.h"
#include "config.h"
#include "ECS/ecs.h"
#include "eventHandler.h"

namespace fs = std::filesystem;

class Engine;

class App {
public:
    virtual void Init() = 0;
    virtual void Frame(double deltaTime) = 0;
    virtual void Cleanup() = 0;
    virtual void EventCallback(Event event) = 0;
    void SetEnginePointer(Engine *engine) { m_Engine = engine; };

protected:
    Engine *m_Engine;
};

class Engine {
public:
    Engine(App *app, Config *config); 
    void Init();
    void Run();
    void Cleanup();
    void EventCallback(Event event);
    static void EventHook(Event event, void *data);
    double GetTime();
    void CreateMesh(const fs::path& objPath, const fs::path& mtlPath, std::vector<Entity>& results);

    RenderSystem *GetRenderer() { return m_RenderSystem; };
    struct GLFWwindow *GetWindow() { return m_Window; };
    size_t GetFrameNumber() { return m_RenderSystem->GetFrameNumber(); };
    glm::vec2 GetFrameMouseDelta() { return m_EventHandler.mousePos - m_EventHandler.mousePosLastFrame; };
    bool *GetKeysDown() { return m_EventHandler.keysDown; };
    ECS& GetECS() { return m_ecs; };

private:
    void CreateMaterial(const MtlData& data, Material& material);
    void CalculateTangents(Vertex& v1, Vertex& v2, Vertex& v3);

private:
    App *m_App;
    ECS m_ecs;
    EventHandler m_EventHandler;
    struct GLFWwindow *m_Window;

    // Default entities, components, and systems, managed by the engine
    RenderSystem *m_RenderSystem;
    LightSystem *m_LightSystem;
};
