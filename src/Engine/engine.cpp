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

void GLFWResizeCb(GLFWwindow *window, int width, int height)
{
    Engine *engine = static_cast<Engine *>(glfwGetWindowUserPointer(window));
    engine->Resized(width, height);
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
    glfwSetWindowUserPointer(m_Window, this);
    glfwSetFramebufferSizeCallback(m_Window, GLFWResizeCb);
    glfwSetWindowSizeCallback(m_Window, GLFWResizeCb);

    // ECS
    m_ecs = ECS();

    // Default Components
    m_ecs.RegisterComponent<Transform>();
    m_ecs.RegisterComponent<Mesh>();
    m_ecs.RegisterComponent<Camera>();
    m_ecs.RegisterComponent<Material>();

    // Default Systems
    m_RenderSystem = m_ecs.RegisterSystem<RenderSystem>();
    m_ecs.SetSystemSignature<RenderSystem>(m_RenderSystem->GetSignature(&m_ecs));
    m_RenderSystem->Init(m_ecs, m_Window, config);

    // App
    m_App = app;
    m_App->SetEnginePointer(this);
    app->Init();
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
    }
}

void Engine::Cleanup()
{
    m_App->Cleanup();
    m_RenderSystem->Cleanup();
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Engine::Resized(int width, int height)
{
    if (width > 0 && height > 0) {
        m_App->Resized(width, height);
        m_RenderSystem->RequestResize();
    }
}

double Engine::GetTime()
{
    return glfwGetTime();
}

void Engine::CreateMaterial(const fs::path& texturePath, const fs::path& vertexShaderPath, const fs::path& fragmentShaderPath, Material& material)
{
    ImageData texture = ImageData((texturePath.empty()) ? "src/Engine/Resource/Texture/default.png" : texturePath);
    AllocatedImage textureImage = m_RenderSystem->AllocateImage(texture);
    MaterialInfo materialInfo = {
        .vertexShader = (vertexShaderPath.empty()) ? "src/Engine/Resource/Shader/basic.vert.spirv" : vertexShaderPath,
        .fragmentShader = (vertexShaderPath.empty()) ? "src/Engine/Resource/Shader/basic.frag.spirv" : fragmentShaderPath,
        .textureImage = textureImage,
        .vertexUniformSize = sizeof(glm::vec4),
        .fragmentUniformSize = sizeof(glm::vec4),
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
