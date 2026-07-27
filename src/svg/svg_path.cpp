#include "svg_path.h"
#include "svg_parser_internal.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

static float nextFloat(const char *&p)
{
    while (*p && (isspace(*p) || *p == ','))
        ++p;
    char *end;
    float v = strtof(p, &end);
    p = end;
    return v;
}

static float nextFlag(const char *&p)
{
    while (*p && (isspace(*p) || *p == ','))
        ++p;
    if (*p == '0' || *p == '1')
    {
        float v = (float)(*p - '0');
        ++p;
        return v;
    }
    // malformed input
    return nextFloat(p);
}

std::vector<PathSegment> parsePath(const std::string &d)
{
    std::vector<PathSegment> out;
    const char *p = d.c_str();
    char cmd = 'M';

    while (*p)
    {
        while (*p && (isspace(*p) || *p == ','))
            ++p;
        if (!*p)
            break;
        if (isalpha(*p))
            cmd = *p++;

        bool rel = (islower(cmd) != 0);
        char upper = (char)toupper(cmd);
        PathSegment seg;
        seg.relative = rel;

        switch (upper)
        {
        case 'M':
            seg.cmd = PathCmd::MoveTo;
            seg.args[0] = nextFloat(p);
            seg.args[1] = nextFloat(p);
            out.push_back(seg);
            cmd = rel ? 'l' : 'L';
            break;
        case 'L':
            seg.cmd = PathCmd::LineTo;
            seg.args[0] = nextFloat(p);
            seg.args[1] = nextFloat(p);
            out.push_back(seg);
            break;
        case 'H':
            seg.cmd = PathCmd::HLineTo;
            seg.args[0] = nextFloat(p);
            out.push_back(seg);
            break;
        case 'V':
            seg.cmd = PathCmd::VLineTo;
            seg.args[0] = nextFloat(p);
            out.push_back(seg);
            break;
        case 'C':
            seg.cmd = PathCmd::CubicTo;
            for (int i = 0; i < 6; i++)
                seg.args[i] = nextFloat(p);
            out.push_back(seg);
            break;
        case 'S':
            seg.cmd = PathCmd::CubicTo;
            seg.args[0] = 0;
            seg.args[1] = 0;
            seg.args[2] = nextFloat(p);
            seg.args[3] = nextFloat(p);
            seg.args[4] = nextFloat(p);
            seg.args[5] = nextFloat(p);
            seg.args[6] = 1.f;
            out.push_back(seg);
            break;
        case 'Q':
            seg.cmd = PathCmd::QuadTo;
            for (int i = 0; i < 4; i++)
                seg.args[i] = nextFloat(p);
            out.push_back(seg);
            break;
        case 'T':
            seg.cmd = PathCmd::QuadTo;
            seg.args[0] = 0;
            seg.args[1] = 0;
            seg.args[2] = nextFloat(p);
            seg.args[3] = nextFloat(p);
            seg.args[4] = 1.f;
            out.push_back(seg);
            break;
        case 'A':
            seg.cmd = PathCmd::ArcTo;
            seg.args[0] = nextFloat(p);
            seg.args[1] = nextFloat(p);
            seg.args[2] = nextFloat(p);
            seg.args[3] = nextFlag(p);
            seg.args[4] = nextFlag(p);
            seg.args[5] = nextFloat(p);
            seg.args[6] = nextFloat(p);
            out.push_back(seg);
            break;
        case 'Z':
            seg.cmd = PathCmd::ClosePath;
            out.push_back(seg);
            cmd = 'M';
            break;
        default:
            ++p;
            break;
        }
    }
    return out;
}

std::vector<std::array<float, 2>> parsePoints(const std::string &s)
{
    std::vector<std::array<float, 2>> out;
    const char *p = s.c_str();
    while (*p)
    {
        while (*p && (isspace(*p) || *p == ','))
            ++p;
        if (!*p)
            break;
        char *e1;
        float x = strtof(p, &e1);
        if (e1 == p)
        {
            ++p;
            continue;
        }
        p = e1;
        while (*p && (isspace(*p) || *p == ','))
            ++p;
        char *e2;
        float y = strtof(p, &e2);
        if (e2 == p)
        {
            ++p;
            continue;
        }
        p = e2;
        out.push_back({x, y});
    }
    return out;
}
