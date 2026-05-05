#include "resourceManager.h"
#include "Util/myAssert.h"
#include "fontResource.h"
#include "imageResource.h"
#include "materialResource.h"
#include "modelResource.h"
#include "resourceTypes.h"
#include "System/Render/textureArrayHandler.h"
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
    // ASSERT(m_ResourceMap.contains(path) && "Getting invalid resource");
    // return m_ResourceMap[path];
    auto it = m_ResourceMap.find(path);
    ASSERT(it != m_ResourceMap.end() && "Getting invalid resource");
    return it->second;
}

fs::path ResourceManager::GetPath(Resource resource) const
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
    m_ResourceMap.insert({path, resource});
    m_PathMap.insert({resource, path});
    return resource;
}

void ResourceManager::LoadMaterial(const fs::path& path, std::vector<Resource>& results)
{
    MaterialResource mtl;
    bool res = mtl.Load(path);
    ASSERT(res && "Failed to load material");
    for (SubMaterialResource& subMaterial : mtl.subMaterials) {
        if (subMaterial.ambientTexture.empty()) {
            subMaterial.ambientTexture = m_PathMap[m_DefaultTextures.white];
        } else if (!m_ResourceMap.contains(subMaterial.ambientTexture)) {
            results.push_back(LoadTexture(subMaterial.ambientTexture, IMAGE_LOAD_FLAG_CHECK_TRANSPARENCY));
        }

        if (subMaterial.diffuseTexture.empty()) {
            subMaterial.diffuseTexture = m_PathMap[m_DefaultTextures.white];
        } else if (!m_ResourceMap.contains(subMaterial.diffuseTexture)) {
            results.push_back(LoadTexture(subMaterial.diffuseTexture, IMAGE_LOAD_FLAG_CHECK_TRANSPARENCY));
        }

        if (subMaterial.normalTexture.empty()) {
            subMaterial.normalTexture = m_PathMap[m_DefaultTextures.normal];
        } else if (!m_ResourceMap.contains(subMaterial.normalTexture)) {
            results.push_back(LoadTexture(subMaterial.normalTexture, IMAGE_LOAD_FLAG_NON_COLOR));
        }
    }
    Resource resource = CreateResource<MaterialResource>(std::move(mtl));
    m_ResourceMap.insert({path, resource});
    m_PathMap.insert({resource, path});
    results.push_back(resource);
}

Resource ResourceManager::LoadModel(const fs::path& path)
{
    ModelResource obj;
    bool res = obj.Load(path);
    ASSERT(res && "Failed to load model");
    Resource resource = CreateResource<ModelResource>(std::move(obj));
    m_ResourceMap.insert({path, resource});
    m_PathMap.insert({resource, path});
    return resource;
}

Resource ResourceManager::LoadFont(const fs::path& path)
{
    FontResource ttf;
    bool res = ttf.Load(path);
    ASSERT(res && "Failed to load font");
    Resource resource = CreateResource<FontResource>(std::move(ttf));
    m_ResourceMap.insert({path, resource});
    m_PathMap.insert({resource, path});
    return resource;
}

std::vector<Resource> ResourceManager::LoadAll(const fs::path& sceneDir)
{
    ASSERT(m_ManifestMap.find(sceneDir) == m_ManifestMap.end() && "Scene resources already loaded");

    ResourceManifest manifest;
    bool res = manifest.Load(sceneDir / "MANIFEST.yaml");
    ASSERT(res && "Failed to load manifest file");

    std::vector<Resource> results(100); // Arbitrary

    // Materials
    // NOTE: We stop duplicate resources being added here 
    //       In the future, if multiple scenes access the same exact resource we may have an issue 
    //       For now, enforce each scene having its own resources.
    for (const fs::path& path : manifest.materials) {
        LoadMaterial(sceneDir / path, results);
    }

    // Models
    for (const fs::path& path : manifest.models) {
        results.push_back(LoadModel(sceneDir / path));
    }

    m_ManifestMap.insert({sceneDir, manifest});

    return results;
}

ResourceFuture ResourceManager::LoadAllAsync(const fs::path& sceneDir)
{
    return std::async(std::launch::async, &ResourceManager::LoadAll, this, sceneDir);
}

void ResourceManager::UnloadAll(const fs::path& sceneDir)
{
    ASSERT(m_ManifestMap.find(sceneDir) != m_ManifestMap.end() && "Unloading scene resources that weren't loaded");

    ResourceManifest manifest = m_ManifestMap[sceneDir];

    // Materials
    for (const fs::path& path : manifest.materials) {
        Resource resource = GetResource(sceneDir / path);
        for (const SubMaterialResource& subMaterial : GetData<MaterialResource>(resource).subMaterials) {
            if (m_ResourceMap.contains(subMaterial.ambientTexture)) {
                Resource subResource = m_ResourceMap[subMaterial.ambientTexture];
                if (subResource != m_DefaultTextures.white) {
                    DestroyResource(subResource);
                }
            }

            if (m_ResourceMap.contains(subMaterial.diffuseTexture)) {
                Resource subResource = m_ResourceMap[subMaterial.diffuseTexture];
                if (subResource != m_DefaultTextures.white) {
                    DestroyResource(subResource);
                }
            }

            if (m_ResourceMap.contains(subMaterial.normalTexture)) {
                Resource subResource = m_ResourceMap[subMaterial.normalTexture];
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

    m_ManifestMap.erase(sceneDir);
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
    // ASSERT(m_ManifestMap.find(sceneDir) != m_ManifestMap.end() && "Getting manifest before it is loaded");
    // return m_ManifestMap[sceneDir];
    auto it = m_ManifestMap.find(sceneDir);
    ASSERT(it != m_ManifestMap.end() && "Getting manifest before it is loaded");
    return it->second;
}

TextureArraySizes ResourceManager::GetArraySizes(const ResourceManifest& manifest) const
{
    // Enough space for default textures
    TextureArraySizes sizes = {
        .color1024 = 3,
        .color2048 = 0,
        .data1024 = 1,
        .data2048 = 0,
        .font = 1
    };

    std::set<fs::path> counted;

    // TODO: Count fonts

    for (const fs::path& path : manifest.materials) {
        Resource resource = GetResource(manifest.sceneDir / path);
        for (const SubMaterialResource& subMaterial : GetData<MaterialResource>(resource).subMaterials) {
            if (!counted.contains(subMaterial.ambientTexture)) {
                const ImageResource& img = GetData<ImageResource>(GetResource(subMaterial.ambientTexture));
                if (img.size.x <= 1024 && img.size.y <= 1024) {
                    sizes.color1024++;
                } else if (img.size.y <= 2048 && img.size.y <= 2048) {
                    sizes.color2048++;
                } else {
                    FATAL("Image too large to allocate in texture array");
                }
                counted.insert(subMaterial.ambientTexture);
            }

            if (!counted.contains(subMaterial.diffuseTexture)) {
                const ImageResource& img = GetData<ImageResource>(GetResource(subMaterial.diffuseTexture));
                if (img.size.x <= 1024 && img.size.y <= 1024) {
                    sizes.color1024++;
                } else if (img.size.y <= 2048 && img.size.y <= 2048) {
                    sizes.color2048++;
                } else {
                    FATAL("Image too large to allocate in texture array");
                }
                counted.insert(subMaterial.diffuseTexture);
            }

            if (!counted.contains(subMaterial.normalTexture)) {
                const ImageResource& img = GetData<ImageResource>(GetResource(subMaterial.normalTexture));
                if (img.size.x <= 1024 && img.size.y <= 1024) {
                    sizes.data1024++;
                } else if (img.size.y <= 2048 && img.size.y <= 2048) {
                    sizes.data2048++;
                } else {
                    FATAL("Image too large to allocate in texture array");
                }
                counted.insert(subMaterial.normalTexture);
            }
        }
    }

    // Minimum of 1 layer even if unused
    sizes.color1024 = std::max(sizes.color1024, 1u);
    sizes.color2048 = std::max(sizes.color2048, 1u);
    sizes.data1024 = std::max(sizes.data1024, 1u);
    sizes.data2048 = std::max(sizes.data2048, 1u);
    sizes.font = std::max(sizes.font, 1u);

    return sizes;
}
