#include "engine.h"

#include "Component/shadowcaster.h"
#include "ECS/ecs.h"
#include "ECS/ecsTypes.h"
#include "Util/objLoader.h"
#include "Util/imageLoader.h"
#include "Util/profiler.h"
#include "scene.h"

#include <ranges>

#define VMA_IMPLEMENTATION
#include <VulkanMemoryAllocator/vk_mem_alloc.h>

#include <GLFW/glfw3.h>

void GLFWErrorCb(int error, const char *desc) 
{
    (void) error;
    ERROR(desc);
}

Engine::Engine(Config& config)
{
    ASSERT(config.windowWidth > 0 && "Window width must not be 0");
    ASSERT(config.windowHeight > 0 && "Window height must not be 0");
    ASSERT(config.msaaSamples > 0 && "MSAA samples must not be 0");
    ASSERT(std::floor(std::log2(config.msaaSamples)) == std::log2(config.msaaSamples) && "MSAA sample count must be a power of 2");
    ASSERT(config.maxScenes > 0 && "Maximum scenes must not be 0");

    // Window
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    window = glfwCreateWindow(config.windowWidth, config.windowHeight, config.appName, nullptr, nullptr);

    glfwSetErrorCallback(GLFWErrorCb);

    // Event Handler
    eventManager.Init(window);
    eventManager.SetEventCallback(Engine::EventHook, this);

    sceneManager.Init(config.maxScenes);

    renderBackend = new RenderBackend();
    renderBackend->Init(window, config);
}

void Engine::Run()
{
    double lastTime = GetTime();
    while (!glfwWindowShouldClose(window)) {
        PROFILER_PROFILE_SCOPE("Frame");

        double currTime = GetTime();
        double deltaTime = currTime - lastTime;
        lastTime = currTime;

        glfwPollEvents();

        sceneManager.DispatchUpdates(deltaTime);
        eventManager.RecordFrame();
    }
}

void Engine::Cleanup()
{
    sceneManager.Cleanup();

    renderBackend->Cleanup();
    delete renderBackend;

    glfwDestroyWindow(window);
    glfwTerminate();
}

void Engine::EventCallback(Event event)
{
    sceneManager.DispatchEvents(event);
}

void Engine::EventHook(Event event, void *data)
{
    Engine *engine = static_cast<Engine *>(data);
    engine->EventCallback(event);
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

    renderBackend->AllocateMaterialTextures(materialInfo, material);
}

void Engine::CreateMesh(ECS *ecs, const fs::path& objPath, const fs::path& mtlPath, std::vector<Entity>& results)
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
        Entity e = ecs->CreateEntity();

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
        renderBackend->AllocateMesh(mesh);
        ecs->AddComponent<Mesh>(e, mesh);
        results.push_back(e);

        Material mat = materials[s.materialName];
        ecs->AddComponent<Material>(e, mat);
    }

    INFO("Mesh load took " << GetTime() - startTime << "s");
}

void Engine::CreatePointLight(ECS *ecs, const LightCreateInfo& info, Entity& result)
{
    Entity e = ecs->CreateEntity();

    ecs->GetComponent<Transform>(e).Translate(info.position);
    ecs->AddComponent(e, Light(POINT, info.color, info.intensity, info.radius));

    result = e;
}

void Engine::CreateSpotLight(ECS *ecs, const LightCreateInfo& info, Entity& result)
{
    Entity e = ecs->CreateEntity();

    glm::vec3 base = glm::vec3(0.0, 0.0, 1.0);
    glm::vec3 axis = glm::cross(base, -glm::normalize(info.direction));
    float angle = glm::acos(glm::dot(base, -glm::normalize(info.direction)));

    Light light = Light(SPOT, info.color, info.intensity, info.radius, info.innerConeRadians, info.outerConeRadians);
    if (info.shadowcaster) {
        light.direction.w = info.shadowBias;
        Shadowcaster shadowcaster = Shadowcaster(PERSPECTIVE, info.outerConeRadians * 2.0, 0.1, info.radius);
        renderBackend->AllocateShadowcaster(light, shadowcaster);
        ecs->AddComponent<Shadowcaster>(e, shadowcaster);
    }

    ecs->AddComponent<Light>(e, light);
    ecs->GetComponent<Transform>(e).Translate(info.position).Rotate(angle, axis);

    result = e;
}

void Engine::CreateDirectionalLight(ECS *ecs, const LightCreateInfo& info, Entity& result)
{
    Entity e = ecs->CreateEntity();

    glm::vec3 base = glm::vec3(0.0, 0.0, 1.0);
    glm::vec3 axis = glm::cross(base, -glm::normalize(info.direction));
    float angle = glm::acos(glm::dot(base, -glm::normalize(info.direction)));

    Light light = Light(DIRECTIONAL, info.color, info.intensity);
    if (info.shadowcaster) {
        light.direction.w = info.shadowBias;
        Shadowcaster shadowcaster = Shadowcaster(ORTHOGRAPHIC, info.projectionLeft, info.projectionRight, info.projectionBottom, info.projectionTop, 0.1, info.radius);
        renderBackend->AllocateShadowcaster(light, shadowcaster);
        ecs->AddComponent<Shadowcaster>(e, shadowcaster);
    }

    ecs->AddComponent<Light>(e, light);
    ecs->GetComponent<Transform>(e).Rotate(angle, axis).Translate(info.position - glm::normalize(info.direction) * info.distance);

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
