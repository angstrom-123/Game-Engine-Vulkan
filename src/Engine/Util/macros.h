#pragma once 

#include <cstdlib>
#include <iostream>

#define QUOTE(...) #__VA_ARGS__

#define _LOG(name, col, msg) std::cerr << col "[" name "] " __FILE_NAME__ << ":" <<  __func__ << ":" << __LINE__ << ": " << "\033[0m" << msg << std::endl
#define _LOG_STRIPPED(name, col, msg) std::cerr << col "[" name "]: \033[0m" << msg << std::endl

#define INFO(msg) _LOG("INFO", "", msg)
#define ERROR(msg) _LOG("ERROR", "\033[91m", msg)
#define WARN(msg) _LOG("WARN", "\033[93m", msg)
#define HINT(msg) _LOG("HINT", "\033[94m", msg)
#define FATAL(msg) _LOG("FATAL", "\033[91m", msg); abort()
#define UNIMPLEMENTED(msg) _LOG("UNIMPLEMENTED", "\033[91m", msg); abort()
#define UNREACHABLE(msg) _LOG("UNREACHABLE", "\033[91m", msg); abort()

#ifdef DEBUG 
    #define DEBUG_ONLY(block) block
    #define ASSERT(expr) if (!(expr)) { FATAL("Assertion failure: " << QUOTE(expr)); }
#elifdef RELEASE
    #define DEBUG_ONLY(block)
    #define ASSERT_ALL_EQUAL(items, value) 
    #define ASSERT(expr) ((void) sizeof(expr))
#else 
    #error "DEBUG or RELEASE must be specified"
#endif
