#pragma once 

#include <type_traits>

template<typename T> concept Enum = std::is_enum<T>::value;

template<Enum E> constexpr typename std::underlying_type<E>::type EnumBase(E e) 
{
    return static_cast<typename std::underlying_type<E>::type>(e);
}
