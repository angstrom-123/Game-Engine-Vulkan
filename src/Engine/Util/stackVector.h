#pragma once 

#include "Util/myAssert.h"
#include <cstddef>
#include <utility>

const size_t MAX_STACK_VECTOR_SIZE = 256 * 1024;

template<typename T, size_t capacity> class StackVector {
    static_assert(capacity > 0 && capacity * sizeof(T) <= MAX_STACK_VECTOR_SIZE && "Bad stack vector capacity");
public:
    ~StackVector() = default;
    StackVector() = default;

    void PushBack(const T& value) 
    { 
        ASSERT(m_Size < capacity && "Inserting out of bounds");
        m_Data[m_Size++] = value;
    };

    void PushBack(T&& value) 
    { 
        ASSERT(m_Size < capacity && "Inserting out of bounds");
        m_Data[m_Size++] = std::move(value);
    };

    template<class... Args> void EmplaceBack(Args&&... args) 
    { 
        // Ooh fancy
        new (&m_Data[m_Size]) T(std::forward<Args>(args)...);
        m_Size++;
    }

    T& Front()
    {
        return m_Data[0];
    }

    T& Back()
    {
        return m_Data[m_Size - 1];
    }
    T *Data() { return m_Data; }

    const T& Front() const
    {
        return m_Data[0];
    }

    const T& Back() const
    { 
        return m_Data[m_Size - 1];
    }

    const T *Data() const
    { 
        return m_Data;
    }

    bool Empty() const
    {
        return m_Size == 0;
    }

    size_t Size() const
    {
        return m_Size;
    }

    size_t Capacity() const
    {
        return capacity;
    }

    T *begin()
    {
        return m_Data;
    }

    T *end()
    {
        return m_Data + m_Size;
    }

    const T *begin() const
    { 
        return m_Data;
    }

    const T *end() const
    {
        return m_Data + m_Size;
    }

    const T *cbegin() const
    {
        return m_Data;
    }

    const T *cend() const
    {
        return m_Data + m_Size;
    }

    T& operator[](size_t i) 
    { 
        ASSERT(i < capacity && i >= 0 && "Index out of bounds");
        return m_Data[i]; 
    }

    const T& operator[](size_t i) const
    { 
        ASSERT(i < capacity && i >= 0 && "Index out of bounds");
        return m_Data[i]; 
    }

private:
    T m_Data[capacity]{};
    size_t m_Size{0};
};
