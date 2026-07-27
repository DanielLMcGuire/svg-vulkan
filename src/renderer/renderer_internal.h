#pragma once
#include <cstdio>
#include <stdexcept>
#include <vulkan/vulkan.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define VK_CHECK(expr, msg)                                                    \
    do                                                                         \
    {                                                                          \
        VkResult _r = (expr);                                                  \
        if (_r != VK_SUCCESS)                                                  \
        {                                                                      \
            char _buf[256];                                                    \
            snprintf(_buf, sizeof(_buf), "Vulkan error: %s (VkResult=%d)",     \
                     msg, (int)_r);                                            \
            throw std::runtime_error(_buf);                                    \
        }                                                                      \
    } while (0)

#define SAFE_VK(p, fn)                                                         \
    do                                                                         \
    {                                                                          \
        if (p != VK_NULL_HANDLE)                                               \
        {                                                                      \
            fn;                                                                \
            p = VK_NULL_HANDLE;                                                \
        }                                                                      \
    } while (0)

struct Mat4
{
    float m[4][4] = {};
};

#ifdef _WIN32
#ifdef _DEBUG
#define RLOG(fmt, ...)                                                         \
    do                                                                         \
    {                                                                          \
        char _rlbuf[512];                                                      \
        snprintf(_rlbuf, sizeof(_rlbuf), "[RNDR] " fmt "\n", ##__VA_ARGS__);   \
        OutputDebugStringA(_rlbuf);                                            \
        printf("%s", _rlbuf);                                                  \
    } while (0)
#else
#define RLOG(fmt, ...)                                                         \
    do                                                                         \
    {                                                                          \
    } while (0)
#endif
#else
#ifdef _DEBUG
#define RLOG(fmt, ...)                                                         \
    do                                                                         \
    {                                                                          \
        printf("[RNDR] " fmt "\n", ##__VA_ARGS__);                             \
    } while (0)
#else
#define RLOG(fmt, ...)                                                         \
    do                                                                         \
    {                                                                          \
    } while (0)
#endif
#endif
