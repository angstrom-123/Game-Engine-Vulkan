#include <iostream>

#define LOG4x4(mat) INFO(std::endl <<\
                         "      " << mat[0][0] << " " << mat[1][0] << " " << mat[2][0] << " " << mat[3][0] << std::endl << \
                         "      " << mat[0][1] << " " << mat[1][1] << " " << mat[2][1] << " " << mat[3][1] << std::endl << \
                         "      " << mat[0][2] << " " << mat[1][2] << " " << mat[2][2] << " " << mat[3][2] << std::endl << \
                         "      " << mat[0][3] << " " << mat[1][3] << " " << mat[2][3] << " " << mat[3][3])
#define INFO(msg) fprintf(stderr, "[INFO] %s:%s:%d: ", __FILE_NAME__, __func__, __LINE__); std::cerr << msg << std::endl
#define ERROR(msg) fprintf(stderr, "\033[91m[ERROR] %s:%s:%d: \033[0m", __FILE_NAME__, __func__, __LINE__); std::cerr << msg << std::endl

#ifdef DEBUG 
    #define WARN(msg) fprintf(stderr, "\033[93m[WARNING] %s:%s:%d: \033[0m", __FILE_NAME__, __func__, __LINE__); std::cerr << msg << std::endl
#elifdef RELEASE
    #define WARN(msg)
#else 
    #error "DEBUG or RELEASE must be specified"
#endif
