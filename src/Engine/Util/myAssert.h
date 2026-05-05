#pragma once 

#include <cstdlib>
#include "logger.h"

#ifdef DEBUG 
    #define QUOTE(...) #__VA_ARGS__
    #define ASSERT(expr) if (!(expr)) { FATAL("Assertion failure: " << QUOTE(expr)); }
#elifdef RELEASE
    #define ASSERT(expr)
#else 
    #error "DEBUG or RELEASE must be specified"
#endif
