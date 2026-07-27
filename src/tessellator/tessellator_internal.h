#pragma once

#include <array>
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using V2 = std::array<float, 2>;

inline constexpr float PI = 3.14159265358979f;

#ifdef _DEBUG
#ifdef _WIN32
#define TESSLOG(fmt, ...)                                                      \
    do                                                                         \
    {                                                                          \
        char _buf[512];                                                        \
        snprintf(_buf, sizeof(_buf), "[TESS] " fmt "\n", ##__VA_ARGS__);       \
        OutputDebugStringA(_buf);                                              \
        printf("%s", _buf);                                                    \
    } while (0)
#else
#define TESSLOG(fmt, ...)                                                      \
    do                                                                         \
    {                                                                          \
        fprintf(stderr, "[TESS] " fmt "\n", ##__VA_ARGS__);                    \
    } while (0)
#endif
#else
#define TESSLOG(fmt, ...)                                                      \
    do                                                                         \
    {                                                                          \
    } while (0)
#endif
