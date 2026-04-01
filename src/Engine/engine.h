#pragma once 

#include <filesystem>

#include "Component/mesh.h"
#include "System/Render/renderSystem.h"
#include "config.h"
#include "ECS/ecs.h"

namespace fs = std::filesystem;

class Engine;

class App {
public:
    virtual void Init() = 0;
    virtual void Frame(double deltaTime) = 0;
    virtual void Cleanup() = 0;
    // TODO: Change the resized thing to be generic event?
    // The resize can be handled by the engine once I move it over.
    virtual void Event() = 0;
    virtual void Resized(int width, int height) = 0;
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
    void Resized(int width, int height);
    double GetTime();
    void CreateMaterial(const fs::path& texturePath, const fs::path& vertexShaderPath, const fs::path& fragmentShaderPath, Material& material);
    void CreateMesh(const fs::path& path, Mesh& mesh);

    RenderSystem *GetRenderer() { return m_RenderSystem; };
    struct GLFWwindow *GetWindow() { return m_Window; };
    size_t GetFrameNumber() { return m_RenderSystem->GetFrameNumber(); };
    ECS& GetECS() { return m_ecs; };

private:
    App *m_App;
    ECS m_ecs;
    struct GLFWwindow *m_Window;

    // Default entities and systems, managed by the engine
    RenderSystem *m_RenderSystem;
};
