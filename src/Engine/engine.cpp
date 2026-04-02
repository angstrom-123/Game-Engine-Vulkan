#include "engine.h"
#include "Util/objLoader.h"
#include "Util/imageLoader.h"
#include "Component/camera.h"

#define VMA_IMPLEMENTATION
#include <VulkanMemoryAllocator/vk_mem_alloc.h>

#include <GLFW/glfw3.h>

void GLFWErrorCb(int error, const char *desc) 
{
    (void) error;
    ERROR(desc);
}

Engine::Engine(App *app, Config *config)
{
    // Window
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    m_Window = glfwCreateWindow(config->windowWidth, config->windowHeight, config->appName, nullptr, nullptr);

    glfwSetErrorCallback(GLFWErrorCb);

    // Event Handler
    m_EventHandler.Init(m_Window);
    m_EventHandler.SetEventCallback(Engine::EventHook, this);

    // ECS
    m_ecs = ECS();

    // Default Components
    m_ecs.RegisterComponent<Transform>();
    m_ecs.RegisterComponent<Mesh>();
    m_ecs.RegisterComponent<Camera>();
    m_ecs.RegisterComponent<Material>();

    // Default Systems
    m_RenderSystem = m_ecs.RegisterSystem<RenderSystem>();
    m_ecs.SetSystemSignature<RenderSystem>(m_RenderSystem->GetSignature(m_ecs));
    m_RenderSystem->Init(m_Window, config);

    // App
    m_App = app;
    m_App->SetEnginePointer(this);
    app->Init();

    // Default Entities + Connection to default systems
    // NOTE: App is inited before this so that any systems created there pick up on these components
    Entity camera = m_ecs.CreateEntity();
    m_ecs.AddComponent<Camera>(camera, Camera(glm::vec3(0.0), glm::vec2(config->windowWidth, config->windowHeight), glm::radians(60.0), 0.1, 1000.0));
    m_RenderSystem->SetCamera(camera);

}

void Engine::Run()
{
    double lastTime = GetTime();
    while (!glfwWindowShouldClose(m_Window)) {
        glfwPollEvents();

        double currTime = GetTime();

        m_App->Frame(currTime - lastTime);
        m_RenderSystem->Draw(m_ecs);

        lastTime = currTime;

        m_EventHandler.RecordFrame();
    }
}

void Engine::Cleanup()
{
    m_App->Cleanup();
    m_RenderSystem->Cleanup();
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Engine::EventCallback(Event event)
{
    switch (event.kind) {
        case EVENT_WINDOW_RESIZE:
            m_RenderSystem->RequestResize();
            break;
        default:
            break;
    };

    m_App->EventCallback(event);
}

void Engine::EventHook(Event event, void *data)
{
    Engine *engine = static_cast<Engine *>(data);
    engine->EventCallback(event);
}

double Engine::GetTime()
{
    return glfwGetTime();
}

void Engine::CreateMaterial(Material& material)
{
    CreateMaterial("src/Engine/Resource/Texture/default.png", 
                   "src/Engine/Resource/Shader/basic.vert.spirv", 
                   "src/Engine/Resource/Shader/basic.frag.spirv", material);
}

void Engine::CreateMaterial(const fs::path& texturePath, Material& material)
{
    CreateMaterial(texturePath, 
                   "src/Engine/Resource/Shader/basic.vert.spirv", 
                   "src/Engine/Resource/Shader/basic.frag.spirv", material);
}

void Engine::CreateMaterial(const fs::path& texturePath, const fs::path& vertexShaderPath, const fs::path& fragmentShaderPath, Material& material)
{
    ImageData texture = ImageData(texturePath);
    AllocatedImage textureImage = m_RenderSystem->AllocateImage(texture);
    MaterialInfo materialInfo = {
        .vertexShader = vertexShaderPath,
        .fragmentShader = fragmentShaderPath,
        .textureImage = textureImage
    };
    m_RenderSystem->CreateMaterial(&materialInfo, material);
}

void Engine::CreateMesh(const std::filesystem::path& path, Mesh& mesh)
{
    mesh.allocated = false;
    mesh.vertexCount = 0;

    OBJData *meshData = new OBJData(path);
    if (meshData->corrupted) {
        delete meshData;
        ERROR("Failed to load mesh: " << path);
        return;
    }

    // TODO: Optimise vertex size
    mesh.vertices = new Vertex[meshData->faces.size() * 3];
    
    for (OBJFaceIndices fi : meshData->faces) {
        for (size_t i = 0; i < 3; i++) {
            glm::ivec3 set = fi[i];
            Vertex v = {
                .position = meshData->vertices[OBJFaceIndices::GetVertexIndex(set)],
                .normal = meshData->normals[OBJFaceIndices::GetNormalIndex(set)],
                .uv = meshData->textureCoords[OBJFaceIndices::GetTexCoordIndex(set)],
            };
            mesh.vertices[mesh.vertexCount++] = v;
        }
    }

    delete meshData;

    m_RenderSystem->AllocateMesh(mesh);
}
