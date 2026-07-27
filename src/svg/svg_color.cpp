#include "svg_color.h"
#include "svg_parser_internal.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

float parseLength(const char *s, float percentOf)
{
    if (!s || !*s)
        return 0.f;
    while (*s && *s == ' ')
        ++s;
    char *end;
    float v = strtof(s, &end);
    while (*end == ' ')
        ++end;
    if (!*end)
        return v;
    if (*end == '%')
        return percentOf > 0.f ? v / 100.f * percentOf : v;
    if (end[0] == 'p' && end[1] == 't')
        return v * (96.f / 72.f);
    if (end[0] == 'p' && end[1] == 'c')
        return v * 16.f;
    if (*end == 'p')
        return v; // px
    if (*end == 'm')
        return v * 3.7795f;
    if (*end == 'c')
        return v * 37.795f;
    if (*end == 'i')
        return v * 96.f;
    return v;
}
float parseLength(const std::string &s, float percentOf)
{
    return parseLength(s.c_str(), percentOf);
}

bool parseViewBox(const std::string &s, float &x, float &y, float &w, float &h)
{
    const char *p = s.c_str();
    auto nextF = [&]()
    {
        while (*p && (isspace(*p) || *p == ','))
            ++p;
        char *end;
        float v = strtof(p, &end);
        p = end;
        return v;
    };
    x = nextF();
    y = nextF();
    w = nextF();
    h = nextF();
    return w > 0 && h > 0;
}

static const std::unordered_map<std::string, uint32_t> CSS_COLORS = {
    {"aliceblue", 0xF0F8FFFF},
    {"antiquewhite", 0xFAEBD7FF},
    {"aqua", 0x00FFFFFF},
    {"aquamarine", 0x7FFFD4FF},
    {"azure", 0xF0FFFFFF},
    {"beige", 0xF5F5DCFF},
    {"bisque", 0xFFE4C4FF},
    {"black", 0x000000FF},
    {"blanchedalmond", 0xFFEBCDFF},
    {"blue", 0x0000FFFF},
    {"blueviolet", 0x8A2BE2FF},
    {"brown", 0xA52A2AFF},
    {"burlywood", 0xDEB887FF},
    {"cadetblue", 0x5F9EA0FF},
    {"chartreuse", 0x7FFF00FF},
    {"chocolate", 0xD2691EFF},
    {"coral", 0xFF7F50FF},
    {"cornflowerblue", 0x6495EDFF},
    {"cornsilk", 0xFFF8DCFF},
    {"crimson", 0xDC143CFF},
    {"cyan", 0x00FFFFFF},
    {"darkblue", 0x00008BFF},
    {"darkcyan", 0x008B8BFF},
    {"darkgoldenrod", 0xB8860BFF},
    {"darkgray", 0xA9A9A9FF},
    {"darkgreen", 0x006400FF},
    {"darkgrey", 0xA9A9A9FF},
    {"darkkhaki", 0xBDB76BFF},
    {"darkmagenta", 0x8B008BFF},
    {"darkolivegreen", 0x556B2FFF},
    {"darkorange", 0xFF8C00FF},
    {"darkorchid", 0x9932CCFF},
    {"darkred", 0x8B0000FF},
    {"darksalmon", 0xE9967AFF},
    {"darkseagreen", 0x8FBC8FFF},
    {"darkslateblue", 0x483D8BFF},
    {"darkslategray", 0x2F4F4FFF},
    {"darkslategrey", 0x2F4F4FFF},
    {"darkturquoise", 0x00CED1FF},
    {"darkviolet", 0x9400D3FF},
    {"deeppink", 0xFF1493FF},
    {"deepskyblue", 0x00BFFFFF},
    {"dimgray", 0x696969FF},
    {"dimgrey", 0x696969FF},
    {"dodgerblue", 0x1E90FFFF},
    {"firebrick", 0xB22222FF},
    {"floralwhite", 0xFFFAF0FF},
    {"forestgreen", 0x228B22FF},
    {"fuchsia", 0xFF00FFFF},
    {"gainsboro", 0xDCDCDCFF},
    {"ghostwhite", 0xF8F8FFFF},
    {"gold", 0xFFD700FF},
    {"goldenrod", 0xDAA520FF},
    {"gray", 0x808080FF},
    {"green", 0x008000FF},
    {"greenyellow", 0xADFF2FFF},
    {"grey", 0x808080FF},
    {"honeydew", 0xF0FFF0FF},
    {"hotpink", 0xFF69B4FF},
    {"indianred", 0xCD5C5CFF},
    {"indigo", 0x4B0082FF},
    {"ivory", 0xFFFFF0FF},
    {"khaki", 0xF0E68CFF},
    {"lavender", 0xE6E6FAFF},
    {"lavenderblush", 0xFFF0F5FF},
    {"lawngreen", 0x7CFC00FF},
    {"lemonchiffon", 0xFFFACDFF},
    {"lightblue", 0xADD8E6FF},
    {"lightcoral", 0xF08080FF},
    {"lightcyan", 0xE0FFFFFF},
    {"lightgoldenrodyellow", 0xFAFAD2FF},
    {"lightgray", 0xD3D3D3FF},
    {"lightgreen", 0x90EE90FF},
    {"lightgrey", 0xD3D3D3FF},
    {"lightpink", 0xFFB6C1FF},
    {"lightsalmon", 0xFFA07AFF},
    {"lightseagreen", 0x20B2AAFF},
    {"lightskyblue", 0x87CEFAFF},
    {"lightslategray", 0x778899FF},
    {"lightslategrey", 0x778899FF},
    {"lightsteelblue", 0xB0C4DEFF},
    {"lightyellow", 0xFFFFE0FF},
    {"lime", 0x00FF00FF},
    {"limegreen", 0x32CD32FF},
    {"linen", 0xFAF0E6FF},
    {"magenta", 0xFF00FFFF},
    {"maroon", 0x800000FF},
    {"mediumaquamarine", 0x66CDAAFF},
    {"mediumblue", 0x0000CDFF},
    {"mediumorchid", 0xBA55D3FF},
    {"mediumpurple", 0x9370DBFF},
    {"mediumseagreen", 0x3CB371FF},
    {"mediumslateblue", 0x7B68EEFF},
    {"mediumspringgreen", 0x00FA9AFF},
    {"mediumturquoise", 0x48D1CCFF},
    {"mediumvioletred", 0xC71585FF},
    {"midnightblue", 0x191970FF},
    {"mintcream", 0xF5FFFAFF},
    {"mistyrose", 0xFFE4E1FF},
    {"moccasin", 0xFFE4B5FF},
    {"navajowhite", 0xFFDEADFF},
    {"navy", 0x000080FF},
    {"oldlace", 0xFDF5E6FF},
    {"olive", 0x808000FF},
    {"olivedrab", 0x6B8E23FF},
    {"orange", 0xFFA500FF},
    {"orangered", 0xFF4500FF},
    {"orchid", 0xDA70D6FF},
    {"palegoldenrod", 0xEEE8AAFF},
    {"palegreen", 0x98FB98FF},
    {"paleturquoise", 0xAFEEEEFF},
    {"palevioletred", 0xDB7093FF},
    {"papayawhip", 0xFFEFD5FF},
    {"peachpuff", 0xFFDAB9FF},
    {"peru", 0xCD853FFF},
    {"pink", 0xFFC0CBFF},
    {"plum", 0xDDA0DDFF},
    {"powderblue", 0xB0E0E6FF},
    {"purple", 0x800080FF},
    {"rebeccapurple", 0x663399FF},
    {"red", 0xFF0000FF},
    {"rosybrown", 0xBC8F8FFF},
    {"royalblue", 0x4169E1FF},
    {"saddlebrown", 0x8B4513FF},
    {"salmon", 0xFA8072FF},
    {"sandybrown", 0xF4A460FF},
    {"seagreen", 0x2E8B57FF},
    {"seashell", 0xFFF5EEFF},
    {"sienna", 0xA0522DFF},
    {"silver", 0xC0C0C0FF},
    {"skyblue", 0x87CEEBFF},
    {"slateblue", 0x6A5ACDFF},
    {"slategray", 0x708090FF},
    {"slategrey", 0x708090FF},
    {"snow", 0xFFFAFAFF},
    {"springgreen", 0x00FF7FFF},
    {"steelblue", 0x4682B4FF},
    {"tan", 0xD2B48CFF},
    {"teal", 0x008080FF},
    {"thistle", 0xD8BFD8FF},
    {"tomato", 0xFF6347FF},
    {"transparent", 0x00000000},
    {"turquoise", 0x40E0D0FF},
    {"violet", 0xEE82EEFF},
    {"wheat", 0xF5DEB3FF},
    {"white", 0xFFFFFFFF},
    {"whitesmoke", 0xF5F5F5FF},
    {"yellow", 0xFFFF00FF},
    {"yellowgreen", 0x9ACD32FF}};

struct CSSArg
{
    float val;
    bool pct;
};
static std::vector<CSSArg> parseCSSArgs(const char *p, const char *end)
{
    std::vector<CSSArg> out;
    while (p < end)
    {
        while (p < end &&
               (isspace((unsigned char)*p) || *p == ',' || *p == '/'))
            ++p;
        if (p >= end)
            break;
        char *e;
        float v = strtof(p, &e);
        if (e == p)
        {
            ++p;
            continue;
        }
        bool pct = (*e == '%');
        if (pct)
            ++e;
        while (*e && isalpha((unsigned char)*e))
            ++e;
        out.push_back({v, pct});
        p = e;
    }
    return out;
}

static Color hslToRgb(float h, float s, float l, float a = 1.f)
{
    h = fmodf(h, 360.f);
    if (h < 0.f)
        h += 360.f;
    float c = (1.f - fabsf(2.f * l - 1.f)) * s;
    float x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f));
    float m = l - c * 0.5f;
    float r, g, b;
    int seg = (int)(h / 60.f) % 6;
    switch (seg)
    {
    case 0:
        r = c;
        g = x;
        b = 0;
        break;
    case 1:
        r = x;
        g = c;
        b = 0;
        break;
    case 2:
        r = 0;
        g = c;
        b = x;
        break;
    case 3:
        r = 0;
        g = x;
        b = c;
        break;
    case 4:
        r = x;
        g = 0;
        b = c;
        break;
    default:
        r = c;
        g = 0;
        b = x;
        break;
    }
    return {r + m, g + m, b + m, a};
}

Color parseColor(const std::string &s)
{
    if (s.empty() || s == "none")
        return Color::none();

    if (s[0] == '#')
    {
        std::string hex = s.substr(1);
        if (hex.size() == 3)
            hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
        if (hex.size() == 4)
            hex = {hex[0], hex[0], hex[1], hex[1],
                   hex[2], hex[2], hex[3], hex[3]};
        if (hex.size() == 6)
            hex += "ff";
        return Color::fromHex((uint32_t)strtoul(hex.c_str(), nullptr, 16));
    }

    if (s.rfind("rgb", 0) == 0)
    {
        auto lp = s.find('('), rp = s.find(')');
        if (lp != std::string::npos && rp != std::string::npos)
        {
            auto args = parseCSSArgs(s.c_str() + lp + 1, s.c_str() + rp);
            if (args.size() >= 3)
            {
                auto chan = [](CSSArg a)
                { return a.pct ? a.val / 100.f : a.val / 255.f; };
                float r = chan(args[0]), g = chan(args[1]), b = chan(args[2]);
                float a = 1.f;
                if (args.size() >= 4)
                    a = args[3].pct ? args[3].val / 100.f : args[3].val;
                return {r, g, b, a};
            }
        }
    }

    if (s.rfind("hsl", 0) == 0)
    {
        auto lp = s.find('('), rp = s.find(')');
        if (lp != std::string::npos && rp != std::string::npos)
        {
            auto args = parseCSSArgs(s.c_str() + lp + 1, s.c_str() + rp);
            if (args.size() >= 3)
            {
                float h = args[0].val;
                float sl = args[1].pct ? args[1].val / 100.f : args[1].val;
                float l = args[2].pct ? args[2].val / 100.f : args[2].val;
                float a = 1.f;
                if (args.size() >= 4)
                    a = args[3].pct ? args[3].val / 100.f : args[3].val;
                return hslToRgb(h, sl, l, a);
            }
        }
    }

    if (s == "currentColor" || s == "inherit")
        return {0, 0, 0, 1};
    auto it = CSS_COLORS.find(s);
    if (it != CSS_COLORS.end())
        return Color::fromHex(it->second);
    SVGLOG("WARNING: unknown color '%s', defaulting to black", s.c_str());
    return {0, 0, 0, 1};
}
