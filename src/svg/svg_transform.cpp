#include "svg_transform.h"
#include "svg_parser_internal.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

static const float PI = 3.14159265f;

Mat3 parseTransform(const std::string &t)
{
    Mat3 result = Mat3::identity();
    const char *p = t.c_str();
    while (*p)
    {
        while (*p && (isspace(*p) || *p == ','))
            ++p;
        if (!*p)
            break;
        std::string fn;
        while (*p && *p != '(')
            fn += *p++;
        if (*p == '(')
            ++p;

        while (!fn.empty() && fn.front() == ' ')
            fn.erase(fn.begin());
        while (!fn.empty() && fn.back() == ' ')
            fn.pop_back();

        std::vector<float> args;
        while (*p && *p != ')')
        {
            while (*p && (isspace(*p) || *p == ','))
                ++p;
            if (*p == ')')
                break;
            char *end;
            float v = strtof(p, &end);
            if (end == p)
            {
                ++p;
                continue;
            }
            args.push_back(v);
            p = end;
        }
        if (*p == ')')
            ++p;

        Mat3 m = Mat3::identity();
        if (fn == "translate")
        {
            float tx = args.size() > 0 ? args[0] : 0.f;
            float ty = args.size() > 1 ? args[1] : 0.f;
            m = Mat3::translate(tx, ty);
        }
        else if (fn == "scale")
        {
            float sx = args.size() > 0 ? args[0] : 1.f;
            float sy = args.size() > 1 ? args[1] : sx;
            m = Mat3::scale(sx, sy);
        }
        else if (fn == "rotate")
        {
            float a = (args.size() > 0 ? args[0] : 0.f) * PI / 180.f;
            if (args.size() >= 3)
            {
                m = Mat3::translate(args[1], args[2]) * Mat3::rotate(a) *
                    Mat3::translate(-args[1], -args[2]);
            }
            else
            {
                m = Mat3::rotate(a);
            }
        }
        else if (fn == "matrix" && args.size() == 6)
        {
            // SVG matrix(a,b,c,d,e,f) [a c e / b d f / 0 0 1]
            // Mat3 m[row][col], result = point * M  (i.e. row-vector)
            // SVG: x' = a*x + c*y + e,  y' = b*x + d*y + f
            // m.apply(x,y) = [m[0][0]*x + m[1][0]*y + m[2][0],
            //                    m[0][1]*x + m[1][1]*y + m[2][1]]
            // finally, m[0][0]=a, m[0][1]=b, m[1][0]=c, m[1][1]=d, m[2][0]=e,
            // m[2][1]=f
            // kill me
            m.m[0][0] = args[0];
            m.m[0][1] = args[1];
            m.m[1][0] = args[2];
            m.m[1][1] = args[3];
            m.m[2][0] = args[4];
            m.m[2][1] = args[5];
        }
        else if (fn == "skewX")
        {
            m.m[1][0] = tanf(args[0] * PI / 180.f);
        }
        else if (fn == "skewY")
        {
            m.m[0][1] = tanf(args[0] * PI / 180.f);
        }
        else if (!fn.empty())
        {
            SVGLOG("WARNING: unknown transform '%s'", fn.c_str());
        }
        result = result * m;
    }
    return result;
}
