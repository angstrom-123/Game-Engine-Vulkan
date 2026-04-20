#include "engine.h"

#include "Component/shadowcaster.h"
#include "System/lightSystem.h"
#include "Util/objLoader.h"
#include "Util/imageLoader.h"
#include "Component/camera.h"
#include "Util/profiler.h"

#include <ranges>

#define VMA_IMPLEMENTATION
#include <VulkanMemoryAllocator/vk_mem_alloc.h>

#include <GLFW/glfw3.h>

void GLFWErrorCb(int error, const char *desc) 
{
    (void) error;
    ERROR(desc);
}

Engine::Engine(App *app, Config& config)
{
    // Window
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    m_Window = glfwCreateWindow(config.windowWidth, config.windowHeight, config.appName, nullptr, nullptr);

    glfwSetErrorCallback(GLFWErrorCb);

    // Event Handler
    m_EventHandler.Init(m_Window);
    m_EventHandler.SetEventCallback(Engine::EventHook, this);

    ECS& ecs = ECS::Get();

    // Register Components
    ecs.RegisterComponent<Transform>();
    ecs.RegisterComponent<Mesh>();
    ecs.RegisterComponent<Camera>();
    ecs.RegisterComponent<Material>();
    ecs.RegisterComponent<Light>();
    ecs.RegisterComponent<Shadowcaster>();

    // Default Systems
    m_RenderSystem = ecs.RegisterSystem<RenderSystem>();
    ecs.SetSystemSignature<RenderSystem>(m_RenderSystem->GetSignature());
    m_RenderSystem->Init(m_Window, config);

    m_LightSystem = ecs.RegisterSystem<LightSystem>();
    ecs.SetSystemSignature<LightSystem>(m_LightSystem->GetSignature());

    m_ShadowSystem = ecs.RegisterSystem<ShadowSystem>();
    ecs.SetSystemSignature<ShadowSystem>(m_ShadowSystem->GetSignature());

    // App
    m_App = app;
    m_App->SetEnginePointer(this);
    app->Init();

    // Default Entities + Connection to default systems
    // NOTE: App is inited before this so that any systems created there pick up on these components
    Entity camera = ecs.CreateEntity();
    ecs.AddComponent<Camera>(camera, Camera(glm::vec3(0.0), glm::vec2(config.windowWidth, config.windowHeight), glm::radians(60.0), 0.01, 1000.0));
    m_RenderSystem->SetCamera(camera);
}

void Engine::Run()
{
    double lastTime = GetTime();
    while (!glfwWindowShouldClose(m_Window)) {
        PROFILER_PROFILE_SCOPE("Frame");

        glfwPollEvents();

        double currTime = GetTime();

        m_App->Frame(currTime - lastTime);

        m_LightSystem->Update();
        m_ShadowSystem->Update();
        m_RenderSystem->Update();

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
        if (ambientTexture.LoadImage(data.ambientTexture, false, IMAGE_KIND_COLOR)) {
            materialInfo.ambientTextureData = &ambientTexture;
        }
    }

    // NOTE: Getting the alpha from diffuse texture, so we check transparency here
    ImageData diffuseTexture;
    if (!data.diffuseTexture.empty()) {
        INFO("Loading image: " << data.diffuseTexture);
        if (diffuseTexture.LoadImage(data.diffuseTexture, true, IMAGE_KIND_COLOR)) {
            materialInfo.diffuseTextureData = &diffuseTexture;
            material.hasTransparency = diffuseTexture.hasTransparency;
        }
    }

    ImageData normalTexture;
    if (!data.normalTexture.empty()) {
        INFO("Loading image: " << data.normalTexture);
        if (normalTexture.LoadImage(data.normalTexture, false, IMAGE_KIND_NORMAL)) {
            materialInfo.normalTextureData = &normalTexture;
        }
    }

    // If any textures are not present (or just not described in the mtl file)
    // the renderer falls back to defaults.
    m_RenderSystem->AllocateMaterialTextures(materialInfo, material);
}

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

    ECS& ecs = ECS::Get();

    // TODO: Optimise vertex size
    //       SNORM for the normals and uvs
    results.reserve(shapes.size());
    for (const Shape& s : shapes) {
        Entity e = ecs.CreateEntity();

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
        ecs.AddComponent<Mesh>(e, mesh);
        results.push_back(e);

        Material mat = materials[s.materialName];
        ecs.AddComponent<Material>(e, mat);
    }

    INFO("Mesh load took " << GetTime() - startTime << "s");
}

void Engine::CreatePointLight(const LightCreateInfo& info, Entity& result)
{
    ASSERT(result == INVALID_HANDLE && "Overwriting existing entity");

    ECS& ecs = ECS::Get();
    Entity e = ecs.CreateEntity();

    ecs.GetComponent<Transform>(e).Translate(info.position);
    ecs.AddComponent(e, Light(POINT, info.color, info.intensity, info.radius));

    result = e;
}

void Engine::CreateSpotLight(const LightCreateInfo& info, Entity& result)
{
    ASSERT(result == INVALID_HANDLE && "Overwriting existing entity");

    ECS& ecs = ECS::Get();
    Entity e = ecs.CreateEntity();

    glm::vec3 base = glm::vec3(0.0, 0.0, 1.0);
    glm::vec3 axis = glm::cross(base, -glm::normalize(info.direction));
    float angle = glm::acos(glm::dot(base, -glm::normalize(info.direction)));

    Light light = Light(SPOT, info.color, info.intensity, info.radius, info.innerConeRadians, info.outerConeRadians);
    if (info.shadowcaster) {
        light.direction.w = info.shadowBias;
        Shadowcaster shadowcaster = Shadowcaster(PERSPECTIVE, info.outerConeRadians, 0.1, info.radius);
        m_RenderSystem->AllocateShadowcaster(light, shadowcaster);
        ecs.AddComponent<Shadowcaster>(e, shadowcaster);
    }

    ecs.AddComponent<Light>(e, light);
    ecs.GetComponent<Transform>(e).Translate(info.position).Rotate(angle, axis);

    result = e;
}

void Engine::CreateDirectionalLight(const LightCreateInfo& info, Entity& result)
{
    ASSERT(result == INVALID_HANDLE && "Overwriting existing entity");

    ECS& ecs = ECS::Get();
    Entity e = ecs.CreateEntity();

    glm::vec3 base = glm::vec3(0.0, 0.0, 1.0);
    glm::vec3 axis = glm::cross(base, -glm::normalize(info.direction));
    float angle = glm::acos(glm::dot(base, -glm::normalize(info.direction)));

    Light light = Light(DIRECTIONAL, info.color, info.intensity);
    if (info.shadowcaster) {
        light.direction.w = info.shadowBias;
        Shadowcaster shadowcaster = Shadowcaster(ORTHOGRAPHIC, info.projectionLeft, info.projectionRight, info.projectionBottom, info.projectionTop, 0.1, info.radius);
        m_RenderSystem->AllocateShadowcaster(light, shadowcaster);
        ecs.AddComponent<Shadowcaster>(e, shadowcaster);
    }

    ecs.AddComponent<Light>(e, light);
    ecs.GetComponent<Transform>(e).Rotate(angle, axis).Translate(info.position - glm::normalize(info.direction) * info.distance);

    result = e;
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
