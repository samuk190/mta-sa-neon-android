// Host stand-in for the NDK <android/log.h>. Only used by the headless build
// under tools/headless; the Android build never sees this directory.
#pragma once

#include <cstdarg>
#include <cstdio>

enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT,
    ANDROID_LOG_VERBOSE,
    ANDROID_LOG_DEBUG,
    ANDROID_LOG_INFO,
    ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR,
    ANDROID_LOG_FATAL,
    ANDROID_LOG_SILENT,
};

inline int __android_log_print(int prio, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

inline int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    std::fprintf(stderr, "[%s/%d] ", tag ? tag : "-", prio);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
    return 0;
}
