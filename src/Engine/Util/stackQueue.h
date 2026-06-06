#pragma once

#include "Util/macros.h"
#include <cstddef>
#include <utility>

const size_t MAX_STACK_QUEUE_SIZE = 256 * 1024;

template<typename T, size_t capacity> class StackQueue {
    static_assert(capacity > 0 && capacity * sizeof(T) <= MAX_STACK_QUEUE_SIZE && "Bad stack vector capacity");
public:
    ~StackQueue() = default;
    StackQueue() = default;

    void Push(const T& value)
    {
        ASSERT(m_Size < capacity  && "Queue full");
        m_Data[m_Back++] = value;
        if (m_Back == capacity) {
            m_Back = 0;
        }
        m_Size++;
    }

    void Push(T&& value)
    {
        ASSERT(m_Size < capacity  && "Queue full");
        m_Data[m_Back++] = std::move(value);
        if (m_Back == capacity) {
            m_Back = 0;
        }
        m_Size++;
    }

    template<class... Args> void Emplace(Args&&... args)
    {
        ASSERT(m_Size < capacity  && "Queue full");
        new (&m_Data[m_Back++]) T(std::forward<Args>(args)...);
        if (m_Back == capacity) {
            m_Back = 0;
        }
        m_Size++;
    }

    T Pop()
    {
        ASSERT(m_Size > 0 && "Queue empty");
        const T& value = m_Data[m_Front++];
        if (m_Front == capacity) {
            m_Front = 0;
        }
        return value;
    }

    void Resize(size_t newSize)
    {
        ASSERT(newSize <= capacity && "Resizing out of bounds");
        m_Size = newSize;
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
    size_t m_Front{0};
    size_t m_Back{0};
};
