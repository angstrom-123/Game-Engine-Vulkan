#pragma once

#ifdef DEBUG 
    #include "Util/macros.h"
    #define USE_VALIDATION_LAYERS VK_TRUE 
    #define VK_CHECK(x)\
        do {\
            VkResult __err = x;\
            if (__err) {\
                ERROR("Vulkan error: " << (VkResult) __err);\
                abort();\
            }\
        } while (0);
#elifdef RELEASE
    #define USE_VALIDATION_LAYERS VK_FALSE 
    #define VK_CHECK(x) (void) x
#else 
    #error "DEBUG or RELEASE must be specified"
#endif
