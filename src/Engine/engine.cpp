#include "engine.h"
#include "Util/objLoader.h"
#include "Util/imageLoader.h"
#include "Component/camera.h"
#include <ranges>

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

    // Register Components
    m_ecs.RegisterComponent<Transform>();
    m_ecs.RegisterComponent<Mesh>();
    m_ecs.RegisterComponent<Camera>();
    m_ecs.RegisterComponent<Material>();

    // Default Systems
    m_RenderSystem = m_ecs.RegisterSystem<RenderSystem>();
    m_ecs.SetSystemSignature<RenderSystem>(m_RenderSystem->GetSignature(m_ecs));
    m_RenderSystem->Init(m_Window, config);

    // Instantiate Default Components
    CreateMaterial(m_DefaultMaterial);

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
    INFO("Loading path: " << texturePath);
    ImageData texture = ImageData(texturePath);
    AllocatedImage textureImage = m_RenderSystem->AllocateImage(texture);
    MaterialInfo materialInfo = {
        .vertexShader = vertexShaderPath,
        .fragmentShader = fragmentShaderPath,
        .textureImage = textureImage,
        .hasTransparency = texture.hasTransparency
    };
    m_RenderSystem->CreateMaterial(materialInfo, material);
}

void Engine::CreateMesh(const fs::path& objPath, const fs::path& mtlPath, std::vector<Entity>& results)
{
    // Materials can be created from this by the engine 
    // mtl file contains good things to have in the material struct.
    // Perhaps I need another material data component?
    //      - this one could be per-mesh
    //      - automatically added with the material component? (defaulted out)
    //      - required by renderer
    //      - if meshes are only loaded with this api then user doesn't need to 
    //        worry about adding any components themself,
    //        they just provide the path to the stuff and its created automatically

    double startTime = GetTime();

    ObjLoader loader;
    std::vector<Shape> shapes;
    std::unordered_map<std::string, MtlData> materialData;
    if (!loader.LoadObj(objPath, mtlPath, materialData, shapes)) {
        ERROR("Failed to load mesh: " << objPath);
        return;
    }

    std::unordered_map<std::string, Material> materials;

    for (const auto& [name, data] : materialData) {
        if (data.diffuseTexture.empty()) {
            materials.insert({name, m_DefaultMaterial});
        } else {
            std::string texPath("src/Sandbox/Resource/Model/");
            texPath.append(data.diffuseTexture);

            Material mat;
            CreateMaterial(texPath, mat);
            materials.insert({name, mat});
        }
    }

    // TODO: Optimise vertex size
    //       SNORM for the normals and uvs
    results.reserve(shapes.size());
    for (const Shape& s : shapes) {
        Entity e = m_ecs.CreateEntity();

        Mesh mesh;
        mesh.allocated = false;
        mesh.vertexCount = s.indices.size();
        mesh.vertices = new Vertex[mesh.vertexCount];
        for (const auto& [index, triple] : s.indices | std::ranges::views::enumerate) {
            Vertex vertex = loader.GetVertex(triple);
            mesh.vertices[index] = vertex;
            mesh.bounds.Update(vertex);
        }
        m_RenderSystem->AllocateMesh(mesh);
        m_ecs.AddComponent<Mesh>(e, mesh);
        results.push_back(e);

        Material mat = materials[s.materialName];
        m_ecs.AddComponent<Material>(e, mat);
    }

    INFO("Load took " << GetTime() - startTime << "s");
}
