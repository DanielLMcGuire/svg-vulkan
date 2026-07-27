#pragma once
#include "svg_types.h"
#include "xml_mini.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _DEBUG
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define SVGLOG(fmt, ...)                                                       \
    do                                                                         \
    {                                                                          \
        char _buf[512];                                                        \
        snprintf(_buf, sizeof(_buf), "[SVG]  " fmt "\n", ##__VA_ARGS__);       \
        OutputDebugStringA(_buf);                                              \
        printf("%s", _buf);                                                    \
    } while (0)
#else
#include <cstdio>
#define SVGLOG(fmt, ...)                                                       \
    do                                                                         \
    {                                                                          \
        fprintf(stderr, "[SVG]  " fmt "\n", ##__VA_ARGS__);                    \
    } while (0)
#endif
#else
#define SVGLOG(fmt, ...)                                                       \
    do                                                                         \
    {                                                                          \
    } while (0)
#endif

using StyleSheet = std::unordered_map<std::string, std::string>;

struct ParseCtx
{
    const SVGViewport *vp;
    const StyleSheet *sheet;
    const std::unordered_map<std::string, const xml::Node *> *idMap;
    int useDepth = 0;
};