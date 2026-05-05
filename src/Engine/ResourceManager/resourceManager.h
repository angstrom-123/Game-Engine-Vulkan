#pragma once

#include "ResourceManager/resourceManifest.h"
#include "ResourceManager/resourceTypes.h"
#include "System/Render/textureArrayHandler.h"
#include "resourceArray.h"
#include <filesystem>
#include <future>
#include <queue>
#include <ranges>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

using ResourceFuture = std::future<std::vector<Resource>>;

class ResourceManager {
public:
    ResourceManager();
    ~ResourceManager();
    void Init();
    Resource LoadTexture(const fs::path& path, uint32_t imageLoadFlags);
    void LoadMaterial(const fs::path& path, std::vector<Resource>& results);
    Resource LoadModel(const fs::path& path);
    Resource LoadFont(const fs::path& path);
    std::vector<Resource> LoadAll(const fs::path& sceneDir);
    [[nodiscard]] ResourceFuture LoadAllAsync(const fs::path& sceneDir);
    void UnloadAll(const fs::path& sceneDir);
    Resource GetResource(const fs::path& path) const;
    fs::path GetPath(Resource resource) const;
    void DestroyResource(Resource resource);
    ResourceManifest GetManifest(const fs::path& sceneDir) const;
    TextureArraySizes GetArraySizes(const ResourceManifest& manifest) const;
    DefaultTextures GetDefaultTextures() const { return m_DefaultTextures; }
    DefaultFonts GetDefaultFonts() const { return m_DefaultFonts; }

    template<typename T> bool HasData(Resource resource) const
    {
        std::type_index index = std::type_index(typeid(T));
        ASSERT(resource != INVALID_HANDLE && "Getting data for invalid resource");
        auto it = m_ResourceArrays.find(index);
        ASSERT(it != m_ResourceArrays.end() && "Resource type not registered");
        ResourceArray<T> *array = static_cast<ResourceArray<T> *>(it->second);
        return array->Contains(resource);
    }

    template<typename T> auto FilterHasData(const std::vector<Resource>& resources) const
    {
        return resources | std::views::filter([this](Resource resource) {
            return this->HasData<T>(resource);
        });
    }

    template<typename T> T& GetData(Resource resource) const
    {
        std::type_index index = std::type_index(typeid(T));
        ASSERT(resource != INVALID_HANDLE && "Getting data for invalid resource");
        auto it = m_ResourceArrays.find(index);
        ASSERT(it != m_ResourceArrays.end() && "Resource type not registered");
        ResourceArray<T> *array = static_cast<ResourceArray<T> *>(it->second);
        return array->Get(resource);
    }

private:
    template<typename T> void RegisterResource()
    {
        std::type_index index = std::type_index(typeid(T));
        ASSERT(m_ResourceArrays.find(index) == m_ResourceArrays.end() && "Registering component type twice");
        m_ResourceArrays.insert({index, new ResourceArray<T>()});
    }

    template<typename T> Resource CreateResource(T&& data)
    {
        std::type_index index = std::type_index(typeid(T));
        ASSERT(m_ResourceArrays.find(index) != m_ResourceArrays.end() && "Creating unregistered resource");
        ResourceArray<T> *array = static_cast<ResourceArray<T> *>(m_ResourceArrays[index]);
        ASSERT(m_LiveResources < MAX_RESOURCES && "Creating too many resources");
        Resource resource = m_FreeResources.front();
        m_FreeResources.pop();
        array->Insert(resource, std::forward<T>(data));
        m_LiveResources++;
        return resource;
    }

private:
    DefaultTextures m_DefaultTextures;
    DefaultFonts m_DefaultFonts;
    std::unordered_map<fs::path, ResourceManifest> m_ManifestMap;

    std::queue<Resource> m_FreeResources;
    int32_t m_LiveResources;
    std::unordered_map<std::type_index, ResourceArrayBase *> m_ResourceArrays;
    std::unordered_map<fs::path, Resource> m_ResourceMap;
    std::unordered_map<Resource, fs::path> m_PathMap;
};
