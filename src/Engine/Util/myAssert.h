#pragma once 

#include <cstdlib>

#define VERIFY(expr) if (!(expr)) abort()

#ifdef DEBUG 
    #define ASSERT(expr) if (!(expr)) abort()
#elifdef RELEASE
    #define ASSERT(expr) (void) sizeof(expr)
#else 
    #error "DEBUG or RELEASE must be specified"
#endif

