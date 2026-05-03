#pragma once

#include "Util/myAssert.h"
#include "resourceTypes.h"
#include <unordered_map>

class ResourceArrayBase {
public:
    virtual ~ResourceArrayBase() = default;
    virtual void ResourceDestroyed(Resource resource) = 0;
};

template<typename T> class ResourceArray : public ResourceArrayBase {
public:
    void Insert(Resource resource, T data)
    {
        ASSERT(resource != INVALID_HANDLE);
        ASSERT(!m_ResourceToIndex.contains(resource) && "Adding data to same resource twice");

        m_Resources[m_Count] = data;
        m_ResourceToIndex[resource] = m_Count;
        m_IndexToResource[m_Count] = resource;
        m_Count++;
    }

    void Remove(Resource resource)
    {
        ASSERT(resource != INVALID_HANDLE);
        ASSERT(m_ResourceToIndex.contains(resource) && "Removing data from resource without one");

        int32_t removedIndex = m_ResourceToIndex[resource];
        m_Resources[removedIndex] = m_Resources[m_Count - 1];

        Resource replace = m_IndexToResource[m_Count - 1];
        m_ResourceToIndex[replace] = removedIndex;
        m_IndexToResource[removedIndex] = replace;

        m_Count--;
    }

    T& Get(Resource resource)
    {
        ASSERT(resource != INVALID_HANDLE);
        ASSERT(m_ResourceToIndex.contains(resource) && "Getting data from resource without any");

        return m_Resources[m_ResourceToIndex[resource]];
    }

    bool Contains(Resource resource)
    {
        ASSERT(resource != INVALID_HANDLE);
        return m_ResourceToIndex.contains(resource);
    }

    T *Data()
    {
        return m_Resources;
    }

    int32_t Size()
    {
        return m_Count;
    }

    void ResourceDestroyed(Resource resource) override 
    {
        if (m_ResourceToIndex.find(resource) != m_ResourceToIndex.end()) {
            Remove(resource);
        }
    }

private:
    int32_t m_Count = 0;
    T m_Resources[MAX_OF_EACH_RESOURCE];
    std::unordered_map<Resource, int32_t> m_ResourceToIndex;
    std::unordered_map<int32_t, Resource> m_IndexToResource;
};
