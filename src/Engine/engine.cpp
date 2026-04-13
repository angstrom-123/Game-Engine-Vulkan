#include "engine.h"

#include "System/Render/lightSystem.h"
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
    m_ecs.RegisterComponent<Light>();

    // Default Systems
    m_RenderSystem = m_ecs.RegisterSystem<RenderSystem>();
    m_ecs.SetSystemSignature<RenderSystem>(m_RenderSystem->GetSignature(m_ecs));
    m_RenderSystem->Init(m_Window, config);

    m_LightSystem = m_ecs.RegisterSystem<LightSystem>();
    m_ecs.SetSystemSignature<LightSystem>(m_LightSystem->GetSignature(m_ecs));

    // App
    m_App = app;
    m_App->SetEnginePointer(this);
    app->Init();

    // Default Entities + Connection to default systems
    // NOTE: App is inited before this so that any systems created there pick up on these components
    Entity camera = m_ecs.CreateEntity();
    m_ecs.AddComponent<Camera>(camera, Camera(glm::vec3(0.0), glm::vec2(config->windowWidth, config->windowHeight), glm::radians(60.0), 0.01, 1000.0));
    m_RenderSystem->SetCamera(camera);
}

void Engine::Run()
{
    double lastTime = GetTime();
    while (!glfwWindowShouldClose(m_Window)) {
        glfwPollEvents();

        double currTime = GetTime();

        m_App->Frame(currTime - lastTime);

        m_LightSystem->Update(m_ecs);
        m_RenderSystem->Update(m_ecs);

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

void Engine::CreateMaterial(const MtlData& data, Material& material)
{
    material.specularExponent = data.specularExponent;

    MaterialTextureInfo materialInfo = {
        .ambientTextureData = nullptr,
        .diffuseTextureData = nullptr,
        .normalTextureData = nullptr,
    };

    ImageData ambientTexture;
    if (!data.ambientTexture.empty()) {
        INFO("Loading image: " << data.ambientTexture);
        if (ambientTexture.LoadImage(data.ambientTexture, false)) {
            materialInfo.ambientTextureData = &ambientTexture;
        }
    }

    // NOTE: Getting the alpha from diffuse texture, so we check transparency here
    ImageData diffuseTexture;
    if (!data.diffuseTexture.empty()) {
        INFO("Loading image: " << data.diffuseTexture);
        if (diffuseTexture.LoadImage(data.diffuseTexture, true)) {
            materialInfo.diffuseTextureData = &diffuseTexture;
            material.hasTransparency = diffuseTexture.hasTransparency;
        }
    }

    ImageData normalTexture;
    if (!data.normalTexture.empty()) {
        INFO("Loading image: " << data.normalTexture);
        if (normalTexture.LoadImage(data.normalTexture, false)) {
            materialInfo.normalTextureData = &normalTexture;
        }
    }

    // If any textures are not present (or just not described in the mtl file)
    // the renderer falls back to defaults.
    m_RenderSystem->AllocateMaterialTextures(materialInfo, material);
}

// TODO: Multiple meshes should be able to use same materials without duplication
void Engine::CreateMesh(const fs::path& objPath, const fs::path& mtlPath, std::vector<Entity>& results)
{
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
        Material mat;
        CreateMaterial(data, mat);
        materials.insert({name, mat});
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

            if ((index + 1) % 3 == 0) {
                CalculateTangents(mesh.vertices[index - 2], mesh.vertices[index - 1], mesh.vertices[index]);
            }
        }
        m_RenderSystem->AllocateMesh(mesh);
        m_ecs.AddComponent<Mesh>(e, mesh);
        results.push_back(e);

        Material mat = materials[s.materialName];
        m_ecs.AddComponent<Material>(e, mat);
    }

    INFO("Mesh load took " << GetTime() - startTime << "s");
}

void Engine::CalculateTangents(Vertex& v1, Vertex& v2, Vertex& v3)
{
    glm::vec3 edge1 = v2.position - v1.position;
    glm::vec3 edge2 = v3.position - v1.position;
    glm::vec2 deltaUV1 = v2.uv - v1.uv;
    glm::vec2 deltaUV2 = v3.uv - v1.uv;

    float f = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
    float fInv = 1.0 / f;
    glm::vec4 tangent(
        fInv * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
        fInv * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
        fInv * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z),
        f < 0.0 ? -1.0 : 1.0
    );

    v1.tangent = tangent;
    v2.tangent = tangent;
    v3.tangent = tangent;
}
