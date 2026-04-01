#pragma once

#include <queue>

#include "ecsTypes.h"
#include "Util/myAssert.h"

class EntityManager {
public:
    EntityManager()
    {
        m_LiveEntities = 0;
        for (int32_t i = 0; i < MAX_ENTITIES; i++) {
            m_FreeEntities.push(i);
        }
    }

    Entity CreateEntity()
    {
        VERIFY(m_LiveEntities < MAX_ENTITIES && "Creating too many entities");

        Entity res = m_FreeEntities.front();
        m_FreeEntities.pop();
        m_LiveEntities++;

        return res;
    }

    void DestroyEntity(Entity entity)
    {
        VERIFY(entity != INVALID_HANDLE && entity < m_LiveEntities && "Destroying invalid entity"); 

        m_FreeEntities.push(entity);
        m_Signatures[entity] = (Signature) { 0 };
        m_LiveEntities--;
    }

    void SetSignature(Entity entity, Signature signature)
    {
        VERIFY(entity != INVALID_HANDLE && entity < m_LiveEntities && "Destroying invalid entity"); 

        m_Signatures[entity] = signature;
    }

    Signature& GetSignature(Entity entity)
    {
        VERIFY(entity != INVALID_HANDLE && entity < m_LiveEntities && "Destroying invalid entity"); 

        return m_Signatures[entity];
    }

private:
    std::queue<Entity> m_FreeEntities;
    Signature m_Signatures[MAX_ENTITIES];
    int32_t m_LiveEntities;
};
