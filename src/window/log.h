#ifndef NS_WINDOW_LOG_H
#define NS_WINDOW_LOG_H

#include <android/log.h>

#define NS_LOG_TAG "NativeSurface"

#define NS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, NS_LOG_TAG, __VA_ARGS__)
#define NS_LOGW(...) __android_log_print(ANDROID_LOG_WARN, NS_LOG_TAG, __VA_ARGS__)
#define NS_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, NS_LOG_TAG, __VA_ARGS__)

#endif
