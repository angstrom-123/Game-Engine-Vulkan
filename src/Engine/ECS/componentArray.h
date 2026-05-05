#pragma once

#include <cstring>
#include <unordered_map>

#include "Util/myAssert.h"
#include "ecsTypes.h"

class ComponentArrayBase {
public:
    virtual ~ComponentArrayBase() = default;
    virtual void EntityDestroyed(Entity entity) = 0;
};

template<typename T> class ComponentArray : public ComponentArrayBase {
public:
    void Insert(Entity entity, T component)
    {
        ASSERT(entity != INVALID_HANDLE);
        ASSERT(m_EntityToIndex.find(entity) == m_EntityToIndex.end() && "Adding component to same entity twice");

        m_Components[m_Count] = component;
        m_EntityToIndex[entity] = m_Count;
        m_IndexToEntity[m_Count] = entity;
        m_Count++;
    }

    void Remove(Entity entity)
    {
        ASSERT(entity != INVALID_HANDLE);
        ASSERT(m_EntityToIndex.find(entity) != m_EntityToIndex.end() && "Removing component from entity without one");

        int32_t removedIndex = m_EntityToIndex[entity];
        m_Components[removedIndex] = m_Components[m_Count - 1];

        Entity replace = m_IndexToEntity[m_Count - 1];
        m_EntityToIndex[replace] = removedIndex;
        m_IndexToEntity[removedIndex] = replace;

        m_Count--;
    }

    T& Get(Entity entity)
    {
        ASSERT(entity != INVALID_HANDLE);
        ASSERT(m_EntityToIndex.find(entity) != m_EntityToIndex.end() && "Getting component from entity without one");

        return m_Components[m_EntityToIndex[entity]];
    }

    T *Data()
    {
        return m_Components;
    }

    int32_t Size() const
    {
        return m_Count;
    }

    void EntityDestroyed(Entity entity) override
    {
        if (m_EntityToIndex.find(entity) != m_EntityToIndex.end()) {
            Remove(entity);
        }
    }

private:
    int32_t m_Count = 0;
    T m_Components[MAX_ENTITIES] = {};
    std::unordered_map<Entity, int32_t> m_EntityToIndex;
    std::unordered_map<int32_t, Entity> m_IndexToEntity;
};
