#pragma once

#include <type_traits>

template<class T, class U> concept Derived = std::is_base_of<U, T>::value;
