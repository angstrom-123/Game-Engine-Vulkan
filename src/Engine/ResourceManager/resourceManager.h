#pragma once

#include "ResourceManager/resourceManifest.h"
#include "ResourceManager/resourceTypes.h"
#include "System/Render/textureArrayHandler.h"
#include "resourceArray.h"
#include <filesystem>
#include <future>
#include <queue>
#include <typeindex>
#include <unordered_map>

namespace fs = std::filesystem;

using ResourceFuture = std::future<std::vector<Resource>>;

class ResourceManager {
public:
    ResourceManager();
    ~ResourceManager();
    void Init();
    Resource LoadTexture(const fs::path& path, uint32_t imageLoadFlags);
    std::vector<Resource> LoadAll(const fs::path& sceneDir);
    [[nodiscard]] ResourceFuture LoadAllAsync(const fs::path& sceneDir);
    void UnloadAll(const fs::path& sceneDir);
    Resource GetResource(const fs::path& path);
    fs::path GetPath(Resource resource);
    void DestroyResource(Resource resource);
    ResourceManifest GetManifest(const fs::path& sceneDir);
    TextureArraySizes GetArraySizes(const ResourceManifest& manifest);
    DefaultTextures GetDefaultTextures() { return m_DefaultTextures; }

    template<typename T> bool HasData(Resource resource)
    {
        std::type_index index = std::type_index(typeid(T));
        ASSERT(resource != INVALID_HANDLE && "Getting data for invalid resource");
        ResourceArray<T> *array = static_cast<ResourceArray<T> *>(m_ResourceArrays[index]);
        return array->Contains(resource);
    }

    template<typename T> T& GetData(Resource resource)
    {
        std::type_index index = std::type_index(typeid(T));
        ASSERT(m_ResourceArrays.contains(index) && "Getting data for unregistered resource");
        ASSERT(resource != INVALID_HANDLE && "Getting data for invalid resource");
        ResourceArray<T> *array = static_cast<ResourceArray<T> *>(m_ResourceArrays[index]);
        return array->Get(resource);
    }

private:
    template<typename T> void RegisterResource()
    {
        std::type_index index = std::type_index(typeid(T));
        ASSERT(m_ResourceArrays.find(index) == m_ResourceArrays.end() && "Registering component type twice");
        m_ResourceArrays.insert({index, new ResourceArray<T>()});
    }
    template<typename T> Resource CreateResource(T data)
    {
        std::type_index index = std::type_index(typeid(T));
        ASSERT(m_ResourceArrays.find(index) != m_ResourceArrays.end() && "Creating unregistered resource");
        ResourceArray<T> *array = static_cast<ResourceArray<T> *>(m_ResourceArrays[index]);
        ASSERT(m_LiveResources < MAX_RESOURCES && "Creating too many resources");
        Resource resource = m_FreeResources.front();
        m_FreeResources.pop();
        array->Insert(resource, data);
        m_LiveResources++;
        return resource;
    }

private:
    DefaultTextures m_DefaultTextures;
    std::unordered_map<fs::path, ResourceManifest> m_ManifestMap;

    std::queue<Resource> m_FreeResources;
    int32_t m_LiveResources;
    std::unordered_map<std::type_index, ResourceArrayBase *> m_ResourceArrays;
    std::unordered_map<fs::path, Resource> m_ResourceMap;
    std::unordered_map<Resource, fs::path> m_PathMap;
};
