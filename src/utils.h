#ifndef UTILS_H
#define UTILS_H

#include <cstdio>
#include <ctime>

#define PRINTF_ERR_STAMP(msg) do { \
    std::time_t currentTime = std::time(nullptr); \
    std::printf("Error at line %d, timestamp: %s: %s", __LINE__, std::ctime(&currentTime), msg); \
} while(0)

#endif // UTILS_H
