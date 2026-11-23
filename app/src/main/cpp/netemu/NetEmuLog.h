#pragma once
// Logging for the netc emulator. Android goes to logcat with tag "SAONetEmu",
// host builds go to stderr. NETEMU_TRACE is compiled in only with NETEMU_VERBOSE.

#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define NETEMU_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SAONetEmu", __VA_ARGS__)
#define NETEMU_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "SAONetEmu", __VA_ARGS__)
#define NETEMU_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SAONetEmu", __VA_ARGS__)
#else
#define NETEMU_LOGI(...) do { std::fprintf(stderr, "[netemu] " __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define NETEMU_LOGW(...) do { std::fprintf(stderr, "[netemu:W] " __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define NETEMU_LOGE(...) do { std::fprintf(stderr, "[netemu:E] " __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#endif

#ifdef NETEMU_VERBOSE
#define NETEMU_TRACE(...) NETEMU_LOGI(__VA_ARGS__)
#else
#define NETEMU_TRACE(...) do { } while (0)
#endif
