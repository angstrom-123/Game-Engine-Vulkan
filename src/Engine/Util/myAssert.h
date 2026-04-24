#pragma once 

#include <cstdlib>
#include "logger.h"

#define VERIFY(expr) if (!(expr)) { ERROR("Verification failure"); abort(); }
#define SUGGEST_IF(expr, suggestion) if (expr) { fprintf(stderr, "\033[94m[SUGGESTION] %s:%s:%d: \033[0m", __FILE_NAME__, __func__, __LINE__); std::cerr << suggestion << std::endl; }

#ifdef DEBUG 
    #define ASSERT(expr) if (!(expr)) { ERROR("Assertion failure"); abort(); }
#elifdef RELEASE
    #define ASSERT(expr)
#else 
    #error "DEBUG or RELEASE must be specified"
#endif
