#ifndef NS_WSI_LOG_H
#define NS_WSI_LOG_H

#include <android/log.h>

#define NS_LOG_TAG "NativeSurface"

#include <cstdio>
#define NS_LOGI(...)                                                    \
    do                                                                  \
    {                                                                   \
        __android_log_print(ANDROID_LOG_INFO, NS_LOG_TAG, __VA_ARGS__); \
        std::printf("[I/WSI] " __VA_ARGS__);                            \
        std::printf("\n");                                              \
        std::fflush(stdout);                                            \
    } while (0)
#define NS_LOGW(...)                                                    \
    do                                                                  \
    {                                                                   \
        __android_log_print(ANDROID_LOG_WARN, NS_LOG_TAG, __VA_ARGS__); \
        std::printf("[W/WSI] " __VA_ARGS__);                            \
        std::printf("\n");                                              \
        std::fflush(stdout);                                            \
    } while (0)
#define NS_LOGE(...)                                                     \
    do                                                                   \
    {                                                                    \
        __android_log_print(ANDROID_LOG_ERROR, NS_LOG_TAG, __VA_ARGS__); \
        std::fprintf(stderr, "[E/WSI] " __VA_ARGS__);                    \
        std::fprintf(stderr, "\n");                                      \
        std::fflush(stderr);                                             \
    } while (0)

#endif
