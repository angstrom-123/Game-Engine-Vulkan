#include "resourceManager.h"
#include "System/Render/backendTypes.h"
#include "Util/myAssert.h"
#include "fontResource.h"
#include "imageResource.h"
#include "materialResource.h"
#include "modelResource.h"
#include "resourceTypes.h"
#include <cmath>
#include <filesystem>
#include <future>
#include <set>

ResourceManager::ResourceManager()
{
    m_LiveResources = 0;
    for (int32_t i = 0; i < MAX_RESOURCES; i++) {
        m_FreeResources.push(i);
    }

    RegisterResource<ImageResource>();
    RegisterResource<MaterialResource>();
    RegisterResource<ModelResource>();
    RegisterResource<FontResource>();
}

ResourceManager::~ResourceManager()
{
    for (auto& [index, array] : m_ResourceArrays) {
        delete array;
    }
}

void ResourceManager::Init()
{
    m_DefaultTextures = {
        .white = LoadTexture("src/Engine/Resource/Texture/white.png", IMAGE_LOAD_FLAG_NONE),
        .gray = LoadTexture("src/Engine/Resource/Texture/gray.png", IMAGE_LOAD_FLAG_NONE),
        .black = LoadTexture("src/Engine/Resource/Texture/black.png", IMAGE_LOAD_FLAG_NONE),
        .normal = LoadTexture("src/Engine/Resource/Texture/normal.png", IMAGE_LOAD_FLAG_NON_COLOR)
    };

    m_DefaultFonts = {
        .robotoRegular = LoadFont("src/Engine/Resource/Font/Roboto-Regular.ttf"),
    };
}

Resource ResourceManager::GetResource(const fs::path& path) const
{
    auto it = m_ResourceMap.find(path.generic_string());
    ASSERT(it != m_ResourceMap.end() && "Getting invalid resource");
    return it->second;
}

PathString ResourceManager::GetPath(Resource resource) const
{
    auto it = m_PathMap.find(resource);
    ASSERT(it != m_PathMap.end() && "Getting invalid resource path");
    return it->second;
}

Resource ResourceManager::LoadTexture(const fs::path& path, uint32_t imageLoadFlags)
{
    ImageResource png;
    bool res = png.Load(path, imageLoadFlags);
    ASSERT(res && "Failed to load texture");
    Resource resource = CreateResource<ImageResource>(std::move(png));
    m_ResourceMap.insert({path.generic_string(), resource});
    m_PathMap.insert({resource, path.generic_string()});
    return resource;
}

void ResourceManager::LoadMaterial(const fs::path& path, std::vector<Resource>& results)
{
    MaterialResource mtl;
    bool res = mtl.Load(path);
    ASSERT(res && "Failed to load material");
    for (SubMaterialResource& subMaterial : mtl.subMaterials) {
        if (subMaterial.diffuseTexture.empty()) {
            subMaterial.diffuseTexture = m_PathMap[m_DefaultTextures.white];
        } else if (!m_ResourceMap.contains(subMaterial.diffuseTexture.generic_string())) {
            results.push_back(LoadTexture(subMaterial.diffuseTexture, IMAGE_LOAD_FLAG_CHECK_TRANSPARENCY));
        }

        if (subMaterial.normalTexture.empty()) {
            subMaterial.normalTexture = m_PathMap[m_DefaultTextures.normal];
        } else if (!m_ResourceMap.contains(subMaterial.normalTexture.generic_string())) {
            results.push_back(LoadTexture(subMaterial.normalTexture, IMAGE_LOAD_FLAG_NON_COLOR));
        }
    }
    Resource resource = CreateResource<MaterialResource>(std::move(mtl));
    m_ResourceMap.insert({path.generic_string(), resource});
    m_PathMap.insert({resource, path.generic_string()});
    results.push_back(resource);
}

Resource ResourceManager::LoadModel(const fs::path& path)
{
    ModelResource obj;
    bool res = obj.Load(path);
    ASSERT(res && "Failed to load model");
    Resource resource = CreateResource<ModelResource>(std::move(obj));
    m_ResourceMap.insert({path.generic_string(), resource});
    m_PathMap.insert({resource, path.generic_string()});
    return resource;
}

Resource ResourceManager::LoadFont(const fs::path& path)
{
    FontResource ttf;
    bool res = ttf.Load(path);
    ASSERT(res && "Failed to load font");
    Resource resource = CreateResource<FontResource>(std::move(ttf));
    m_ResourceMap.insert({path.generic_string(), resource});
    m_PathMap.insert({resource, path.generic_string()});
    return resource;
}

std::vector<Resource> ResourceManager::LoadAll(const fs::path& sceneDir)
{
    ASSERT(m_ManifestMap.find(sceneDir.generic_string()) == m_ManifestMap.end() && "Scene resources already loaded");

    ResourceManifest manifest;
    bool res = manifest.Load(sceneDir / "MANIFEST.yaml");
    ASSERT(res && "Failed to load manifest file");

    std::vector<Resource> results(100); // Arbitrary

    // Materials
    // NOTE: We stop duplicate resources being added here 
    //       In the future, if multiple scenes access the same exact resource we may have an issue 
    //       For now, enforce each scene having its own resources.
    for (const fs::path& path : manifest.materials) {
        LoadMaterial((sceneDir / path).generic_string(), results);
    }

    // Models
    for (const fs::path& path : manifest.models) {
        results.push_back(LoadModel(sceneDir / path));
    }

    m_ManifestMap.insert({sceneDir.generic_string(), manifest});

    return results;
}

ResourceFuture ResourceManager::LoadAllAsync(const fs::path& sceneDir)
{
    INFO("Async loading: " << sceneDir);
    return std::async(std::launch::async, &ResourceManager::LoadAll, this, sceneDir);
}

void ResourceManager::UnloadAll(const fs::path& sceneDir)
{
    ASSERT(m_ManifestMap.find(sceneDir.generic_string()) != m_ManifestMap.end() && "Unloading scene resources that weren't loaded");

    ResourceManifest manifest = m_ManifestMap[sceneDir.generic_string()];

    // Materials
    for (const fs::path& path : manifest.materials) {
        Resource resource = GetResource(sceneDir / path);
        for (const SubMaterialResource& subMaterial : GetData<MaterialResource>(resource).subMaterials) {
            if (m_ResourceMap.contains(subMaterial.diffuseTexture.generic_string())) {
                Resource subResource = m_ResourceMap[subMaterial.diffuseTexture.generic_string()];
                if (subResource != m_DefaultTextures.white) {
                    DestroyResource(subResource);
                }
            }

            if (m_ResourceMap.contains(subMaterial.normalTexture.generic_string())) {
                Resource subResource = m_ResourceMap[subMaterial.normalTexture.generic_string()];
                if (subResource != m_DefaultTextures.normal) {
                    DestroyResource(subResource);
                }
            }
        }
        DestroyResource(resource);
    }

    // Models
    for (const fs::path& path : manifest.models) {
        DestroyResource(GetResource(sceneDir / path));
    }

    m_ManifestMap.erase(sceneDir.generic_string());
}

void ResourceManager::DestroyResource(Resource resource)
{
    ASSERT(m_PathMap.contains(resource) && "Destroying invalid resource");
    for (auto& [index, array] : m_ResourceArrays) {
        array->ResourceDestroyed(resource);
    }

    m_ResourceMap.erase(m_PathMap[resource]);
    m_PathMap.erase(resource);
    m_FreeResources.push(resource);
    m_LiveResources--;
}

ResourceManifest ResourceManager::GetManifest(const fs::path& sceneDir) const
{
    auto it = m_ManifestMap.find(sceneDir.generic_string());
    ASSERT(it != m_ManifestMap.end() && "Getting manifest before it is loaded");
    return it->second;
}

void ResourceManager::GetArraySizes(const ResourceManifest& manifest, uint32_t (&results)[static_cast<size_t>(TextureArrayID::MAX_ENUM)], const VulkanBackendSettings& settings) const
{
    // Enough space for default textures
    results[static_cast<size_t>(TextureArrayID::COLOR_SMALL)] = 3;
    results[static_cast<size_t>(TextureArrayID::COLOR_LARGE)] = 1;
    results[static_cast<size_t>(TextureArrayID::DATA_SMALL)] = 1;
    results[static_cast<size_t>(TextureArrayID::DATA_LARGE)] = 1;
    results[static_cast<size_t>(TextureArrayID::FONT)] = 1;

    std::set<fs::path> counted;

    // TODO: Count fonts

    auto Fits = [](glm::ivec2 imageSize, int32_t resolution) {
        return (imageSize.x <= resolution && imageSize.y <= resolution);
    };

    for (const fs::path& path : manifest.materials) {
        Resource resource = GetResource(manifest.sceneDir / path);
        for (const SubMaterialResource& subMaterial : GetData<MaterialResource>(resource).subMaterials) {
            if (!counted.contains(subMaterial.diffuseTexture)) {
                const ImageResource& img = GetData<ImageResource>(GetResource(subMaterial.diffuseTexture));
                if (Fits(img.size, settings.colorSmallResolution)) {
                    results[static_cast<size_t>(TextureArrayID::COLOR_SMALL)]++;
                } else if (Fits(img.size, settings.colorLargeResolution)) {
                    results[static_cast<size_t>(TextureArrayID::COLOR_LARGE)]++;
                } else {
                    FATAL("Image too large to allocate in texture array");
                }
                counted.insert(subMaterial.diffuseTexture);
            }

            if (!counted.contains(subMaterial.normalTexture)) {
                const ImageResource& img = GetData<ImageResource>(GetResource(subMaterial.normalTexture));
                if (Fits(img.size, settings.dataSmallResolution)) {
                    results[static_cast<size_t>(TextureArrayID::DATA_SMALL)]++;
                } else if (Fits(img.size, settings.dataLargeResolution)) {
                    results[static_cast<size_t>(TextureArrayID::DATA_LARGE)]++;
                } else {
                    FATAL("Image too large to allocate in texture array");
                }
                counted.insert(subMaterial.normalTexture);
            }
        }
    }
}
