#include "svg_parser.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <chrono>

#ifdef _DEBUG
    #ifdef _WIN32
        #ifndef WIN32_LEAN_AND_MEAN
            #define WIN32_LEAN_AND_MEAN
        #endif
        #ifndef NOMINMAX
            #define NOMINMAX
        #endif
        #include <windows.h>
        #define SVGLOG(fmt, ...) do { \
            char _buf[512]; \
            snprintf(_buf, sizeof(_buf), "[SVG] " fmt "\n", ##__VA_ARGS__); \
            OutputDebugStringA(_buf); \
            printf("%s", _buf); \
        } while(0)
    #else
        #include <cstdio>
        #define SVGLOG(fmt, ...) do { \
            fprintf(stderr, "[SVG] " fmt "\n", ##__VA_ARGS__); \
        } while(0)
    #endif
#else
    #define SVGLOG(fmt, ...) do {} while(0)
#endif

namespace xml {

struct Attr  { std::string name, value; };
struct Node  {
    std::string              tag;
    std::vector<Attr>        attrs;
    std::vector<Node>        children;

    const std::string* attr(const char* name) const {
        for(auto& a : attrs) if(a.name==name) return &a.value;
        return nullptr;
    }
};

static void skipWS(const char*& p) {
    while(*p && (unsigned char)*p <= 32) ++p;
}

static std::string parseName(const char*& p) {
    std::string out;
    while(*p && *p!='=' && *p!='>' && *p!='/' && (unsigned char)*p>32)
        out += *p++;
    return out;
}

static std::string parseAttrValue(const char*& p) {
    skipWS(p);
    if(*p != '=') return {};
    ++p; skipWS(p);
    if(*p != '"' && *p != '\'') return {};
    char q = *p++;
    std::string out;
    while(*p && *p != q) {
        if(*p == '&') {
            ++p;
            std::string ent;
            while(*p && *p != ';') ent += *p++;
            if(*p) ++p;
            if(ent=="lt")        out+='<';
            else if(ent=="gt")   out+='>';
            else if(ent=="amp")  out+='&';
            else if(ent=="quot") out+='"';
            else if(ent=="apos") out+='\'';
        } else {
            out += *p++;
        }
    }
    if(*p) ++p;
    return out;
}

static Node parseNode(const char*& p);

static void parseChildren(const char*& p, Node& parent) {
    while(*p) {
        skipWS(p);
        if(!*p) break;
        if(p[0]=='<' && p[1]=='/') break;

        if(p[0]=='<' && p[1]=='!') {
            if(p[2]=='-' && p[3]=='-') {
                p += 4;
                while(*p && !(*p=='-' && p[1]=='-' && p[2]=='>')) ++p;
                if(*p) p += 3;
            } else {
                while(*p && *p!='>') ++p;
                if(*p) ++p;
            }
            continue;
        }
        if(*p == '<') {
            parent.children.push_back(parseNode(p));
        } else {
            while(*p && *p!='<') ++p;
        }
    }
}

static Node parseNode(const char*& p) {
    Node node;
    if(*p=='<') ++p;
    if(*p=='?') { while(*p && *p!='>') ++p; if(*p) ++p; return node; }
    if(p[0]=='!' && p[1]=='-') { while(*p && !(*p=='-'&&p[1]=='-'&&p[2]=='>')) ++p; if(*p) p+=3; return node; }

    node.tag = parseName(p);
    if(auto c = node.tag.rfind(':'); c != std::string::npos)
        node.tag = node.tag.substr(c+1);

    while(*p && *p!='>' && *p!='/') {
        skipWS(p);
        if(*p=='>' || *p=='/' || !*p) break;
        Attr a;
        a.name = parseName(p);
        if(a.name.empty()) { ++p; continue; }

        if(auto c = a.name.rfind(':'); c != std::string::npos)
            a.name = a.name.substr(c+1);
        a.value = parseAttrValue(p);
        if(!a.name.empty())
            node.attrs.push_back(std::move(a));
    }
    if(*p=='/') { ++p; if(*p=='>') ++p; return node; }
    if(*p=='>') ++p;

    parseChildren(p, node);

    if(p[0]=='<' && p[1]=='/') {
        p += 2;
        while(*p && *p!='>') ++p;
        if(*p) ++p;
    }
    return node;
}

Node parse(const std::string& src) {
    const char* p = src.c_str();
    skipWS(p);
    while(*p == '<' && (p[1]=='?' || p[1]=='!')) {
        if(p[1]=='!' && p[2]=='-' && p[3]=='-') {
            p += 4;
            while(*p && !(*p=='-' && p[1]=='-' && p[2]=='>')) ++p;
            if(*p) p += 3;
        } else {
            while(*p && *p!='>') ++p;
            if(*p) ++p;
        }
        skipWS(p);
    }
    return parseNode(p);
}

} // namespace xml

static float parseLength(const char* s, float percentOf = 0.f) {
    if(!s || !*s) return 0.f;
    while(*s && *s==' ') ++s;
    char* end;
    float v = strtof(s, &end);
    while(*end==' ') ++end;
    if(!*end || *end=='p') return v;
    if(*end=='%') return percentOf > 0.f ? v/100.f * percentOf : v;
    if(*end=='m') return v * 3.7795f;
    if(*end=='c') return v * 37.795f;
    if(*end=='i') return v * 96.f;
    return v;
}
static float parseLength(const std::string& s, float percentOf = 0.f) {
    return parseLength(s.c_str(), percentOf);
}

static bool parseViewBox(const std::string& s,
    float& x, float& y, float& w, float& h)
{
    const char* p = s.c_str();
    auto nextF = [&]() {
        while(*p && (isspace(*p)||*p==',')) ++p;
        char* end; float v = strtof(p, &end); p = end; return v;
    };
    x = nextF(); y = nextF(); w = nextF(); h = nextF();
    return w > 0 && h > 0;
}

static const std::unordered_map<std::string,uint32_t> CSS_COLORS = {
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
    {"yellowgreen", 0x9ACD32FF}
};

struct CSSArg { float val; bool pct; };
static std::vector<CSSArg> parseCSSArgs(const char* p, const char* end) {
    std::vector<CSSArg> out;
    while(p < end) {
        while(p < end && (isspace((unsigned char)*p) || *p==',' || *p=='/')) ++p;
        if(p >= end) break;
        char* e;
        float v = strtof(p, &e);
        if(e == p) { ++p; continue; }
        bool pct = (*e == '%');
        if(pct) ++e;
        while(*e && isalpha((unsigned char)*e)) ++e;
        out.push_back({v, pct});
        p = e;
    }
    return out;
}

static Color hslToRgb(float h, float s, float l, float a=1.f) {
    h = fmodf(h, 360.f);
    if(h < 0.f) h += 360.f;
    float c = (1.f - fabsf(2.f*l - 1.f)) * s;
    float x = c * (1.f - fabsf(fmodf(h/60.f, 2.f) - 1.f));
    float m = l - c*0.5f;
    float r,g,b;
    int seg = (int)(h/60.f) % 6;
    switch(seg) {
    case 0: r=c; g=x; b=0; break;
    case 1: r=x; g=c; b=0; break;
    case 2: r=0; g=c; b=x; break;
    case 3: r=0; g=x; b=c; break;
    case 4: r=x; g=0; b=c; break;
    default:r=c; g=0; b=x; break;
    }
    return {r+m, g+m, b+m, a};
}

static Color parseColor(const std::string& s) {
    if(s.empty() || s=="none") return Color::none();

    if(s[0]=='#') {
        std::string hex = s.substr(1);
        if(hex.size()==3) hex = {hex[0],hex[0],hex[1],hex[1],hex[2],hex[2]};
        if(hex.size()==4) hex = {hex[0],hex[0],hex[1],hex[1],hex[2],hex[2],hex[3],hex[3]};
        if(hex.size()==6) hex += "ff";
        return Color::fromHex((uint32_t)strtoul(hex.c_str(),nullptr,16));
    }

    if(s.rfind("rgb",0)==0) {
        auto lp = s.find('('), rp = s.find(')');
        if(lp != std::string::npos && rp != std::string::npos) {
            auto args = parseCSSArgs(s.c_str()+lp+1, s.c_str()+rp);
            if(args.size() >= 3) {
                auto chan = [](CSSArg a) { return a.pct ? a.val/100.f : a.val/255.f; };
                float r = chan(args[0]), g = chan(args[1]), b = chan(args[2]);
                float a = 1.f;
                if(args.size() >= 4)
                    a = args[3].pct ? args[3].val/100.f : args[3].val;
                return {r, g, b, a};
            }
        }
    }

    if(s.rfind("hsl",0)==0) {
        auto lp = s.find('('), rp = s.find(')');
        if(lp != std::string::npos && rp != std::string::npos) {
            auto args = parseCSSArgs(s.c_str()+lp+1, s.c_str()+rp);
            if(args.size() >= 3) {
                float h  = args[0].val;
                float sl = args[1].pct ? args[1].val/100.f : args[1].val;
                float l  = args[2].pct ? args[2].val/100.f : args[2].val;
                float a  = 1.f;
                if(args.size() >= 4)
                    a = args[3].pct ? args[3].val/100.f : args[3].val;
                return hslToRgb(h, sl, l, a);
            }
        }
    }

    if(s=="currentColor" || s=="inherit") return {0,0,0,1};
    auto it = CSS_COLORS.find(s);
    if(it != CSS_COLORS.end()) return Color::fromHex(it->second);
    SVGLOG("WARNING: unknown color '%s', defaulting to black", s.c_str());
    return {0,0,0,1};
}

using StyleSheet = std::unordered_map<std::string, std::string>;

static void trimStr(std::string& t) {
    size_t a = t.find_first_not_of(" \t\r\n");
    size_t b = t.find_last_not_of(" \t\r\n");
    t = (a == std::string::npos) ? "" : t.substr(a, b - a + 1);
}

static void parseStyleSheet(const std::string& css, StyleSheet& sheet) {
    std::string src;
    src.reserve(css.size());
    for(size_t i = 0; i < css.size(); ) {
        if(i + 1 < css.size() && css[i] == '/' && css[i+1] == '*') {
            i += 2;
            while(i + 1 < css.size() && !(css[i] == '*' && css[i+1] == '/')) ++i;
            i += 2;
        } else {
            src += css[i++];
        }
    }

    size_t pos = 0;
    while(pos < src.size()) {
        size_t lbrace = src.find('{', pos);
        if(lbrace == std::string::npos) break;
        size_t rbrace = src.find('}', lbrace + 1);
        if(rbrace == std::string::npos) break;

        std::string selectorBlock = src.substr(pos, lbrace - pos);
        std::string declarations  = src.substr(lbrace + 1, rbrace - lbrace - 1);
        trimStr(declarations);
        pos = rbrace + 1;

        trimStr(selectorBlock);
        if(selectorBlock.empty() || selectorBlock[0] == '@') continue;

        std::istringstream selStream(selectorBlock);
        std::string sel;
        while(std::getline(selStream, sel, ',')) {
            trimStr(sel);
            if(sel.empty()) continue;

            if(sel.find(' ') != std::string::npos) continue;
            SVGLOG("  CSS rule: '%s' => '%s'", sel.c_str(), declarations.c_str());

            auto it = sheet.find(sel);
            if(it == sheet.end())
                sheet[sel] = declarations;
            else {
                it->second += ';';
                it->second += declarations;
            }
        }
    }
}

static bool isHidden(const xml::Node& node) {
    if(auto* d = node.attr("display"))    if(*d=="none") return true;
    if(auto* v = node.attr("visibility")) if(*v=="hidden" || *v=="collapse") return true;
    if(auto* s = node.attr("style")) {
        if(s->find("display:none")       != std::string::npos) return true;
        if(s->find("display: none")      != std::string::npos) return true;
        if(s->find("visibility:hidden")  != std::string::npos) return true;
        if(s->find("visibility: hidden") != std::string::npos) return true;
    }
    return false;
}

static void applyDeclarations(const std::string& decls, Style& s) {
    std::istringstream ss(decls);
    std::string token;
    while(std::getline(ss, token, ';')) {
        auto colon = token.find(':');
        if(colon == std::string::npos) continue;
        std::string key = token.substr(0, colon);
        std::string val = token.substr(colon + 1);
        trimStr(key); trimStr(val);
        if(val == "inherit" || key.empty() || val.empty()) continue;
        if(key == "isolation" || key == "mix-blend-mode" || key == "enable-background") continue;
        if(key == "fill") {
            s.fill = (val == "none") ? Paint::transparent() : Paint::solid(parseColor(val));
        } else if(key == "stroke") {
            s.stroke = (val == "none") ? Paint::transparent() : Paint::solid(parseColor(val));
        } else if(key == "stroke-width")     s.strokeWidth = parseLength(val);
        else if(key == "opacity")            s.opacity     = strtof(val.c_str(), nullptr);
        else if(key == "fill-opacity")       s.fillOpacity = strtof(val.c_str(), nullptr);
        else if(key == "stroke-opacity") {
            if(!s.stroke.none) s.stroke.color.a *= strtof(val.c_str(), nullptr);
        } else if(key == "stroke-linecap") {
            if(val == "round")       s.lineCap = LineCap::Round;
            else if(val == "square") s.lineCap = LineCap::Square;
            else                     s.lineCap = LineCap::Butt;
        } else if(key == "stroke-linejoin") {
            if(val == "round")      s.lineJoin = LineJoin::Round;
            else if(val == "bevel") s.lineJoin = LineJoin::Bevel;
            else                    s.lineJoin = LineJoin::Miter;
        } else if(key == "stroke-miterlimit") {
            s.miterLimit = strtof(val.c_str(), nullptr);
        } else if(key == "stroke-dasharray") {
            s.dashArray.clear();
            if(val != "none") {
                const char* p = val.c_str();
                while(*p) {
                    while(*p && (isspace((unsigned char)*p) || *p == ',')) ++p;
                    if(!*p) break;
                    char* e;
                    float v = strtof(p, &e);
                    if(e == p) { ++p; continue; }
                    if(v >= 0.f) s.dashArray.push_back(v);
                    p = e;
                }
                if(!s.dashArray.empty() && s.dashArray.size() % 2 == 1) {
                    auto copy = s.dashArray;
                    s.dashArray.insert(s.dashArray.end(), copy.begin(), copy.end());
                }
            }
        } else if(key == "stroke-dashoffset") {
            s.dashOffset = parseLength(val);
        } else if(key == "fill-rule") {
            s.fillRule = (val == "evenodd") ? FillRule::EvenOdd : FillRule::NonZero;
        } else if(key == "display") {
        }
    }
}

static Style parseStyle(const xml::Node& node, const Style& parent,
                        const StyleSheet& sheet)
{
    Style s = parent;

    for(auto& attr : node.attrs) {
        std::string decl = attr.name + ':' + attr.value;
        applyDeclarations(decl, s);
    }

    {
        auto it = sheet.find(node.tag);
        if(it != sheet.end()) applyDeclarations(it->second, s);
    }
    if(auto* classAttr = node.attr("class")) {
        std::istringstream cs(*classAttr);
        std::string cls;
        while(cs >> cls) {
            auto it = sheet.find('.' + cls);
            if(it != sheet.end()) {
                SVGLOG("  applying CSS class .%s", cls.c_str());
                applyDeclarations(it->second, s);
            }
        }
    }
    if(auto* idAttr = node.attr("id")) {
        auto it = sheet.find('#' + *idAttr);
        if(it != sheet.end()) applyDeclarations(it->second, s);
    }

    if(auto* styleAttr = node.attr("style"))
        applyDeclarations(*styleAttr, s);

    return s;
}

static Mat3 parseTransform(const std::string& t) {
    Mat3 result = Mat3::identity();
    const char* p = t.c_str();
    while(*p) {
        while(*p && (isspace(*p)||*p==',')) ++p;
        if(!*p) break;
        std::string fn;
        while(*p && *p!='(') fn += *p++;
        if(*p=='(') ++p;

        while(!fn.empty() && fn.front()==' ') fn.erase(fn.begin());
        while(!fn.empty() && fn.back() ==' ') fn.pop_back();

        std::vector<float> args;
        while(*p && *p!=')') {
            while(*p && (isspace(*p)||*p==',')) ++p;
            if(*p==')') break;
            char* end;
            float v = strtof(p, &end);
            if(end == p) { ++p; continue; }
            args.push_back(v);
            p = end;
        }
        if(*p==')') ++p;

        Mat3 m = Mat3::identity();
        if(fn=="translate") {
            float tx = args.size()>0 ? args[0] : 0.f;
            float ty = args.size()>1 ? args[1] : 0.f;
            m = Mat3::translate(tx, ty);
        } else if(fn=="scale") {
            float sx = args.size()>0 ? args[0] : 1.f;
            float sy = args.size()>1 ? args[1] : sx;
            m = Mat3::scale(sx, sy);
        } else if(fn=="rotate") {
            float a = (args.size()>0 ? args[0] : 0.f) * 3.14159265f/180.f;
            if(args.size()>=3) {
                m = Mat3::translate(args[1],args[2]) *
                    Mat3::rotate(a) *
                    Mat3::translate(-args[1],-args[2]);
            } else {
                m = Mat3::rotate(a);
            }
        } else if(fn=="matrix" && args.size()==6) {
            // SVG matrix(a,b,c,d,e,f) [a c e / b d f / 0 0 1]
            // Mat3 m[row][col], result = point * M  (i.e. row-vector)
            // SVG: x' = a*x + c*y + e,  y' = b*x + d*y + f
            // m.apply(x,y) = [m[0][0]*x + m[1][0]*y + m[2][0],
            //                    m[0][1]*x + m[1][1]*y + m[2][1]]
            // finally, m[0][0]=a, m[0][1]=b, m[1][0]=c, m[1][1]=d, m[2][0]=e, m[2][1]=f
            // kill me
            m.m[0][0]=args[0]; m.m[0][1]=args[1];
            m.m[1][0]=args[2]; m.m[1][1]=args[3];
            m.m[2][0]=args[4]; m.m[2][1]=args[5];
        } else if(fn=="skewX") {
            m.m[1][0] = tanf(args[0] * 3.14159265f/180.f);
        } else if(fn=="skewY") {
            m.m[0][1] = tanf(args[0] * 3.14159265f/180.f);
        } else if(!fn.empty()) {
            SVGLOG("WARNING: unknown transform '%s'", fn.c_str());
        }
        result = result * m;
    }
    return result;
}

static float nextFloat(const char*& p) {
    while(*p && (isspace(*p)||*p==',')) ++p;
    char* end;
    float v = strtof(p, &end);
    p = end;
    return v;
}

static std::vector<PathSegment> parsePath(const std::string& d) {
    std::vector<PathSegment> out;
    const char* p = d.c_str();
    char cmd = 'M';

    while(*p) {
        while(*p && (isspace(*p)||*p==',')) ++p;
        if(!*p) break;
        if(isalpha(*p)) cmd = *p++;

        bool rel   = (islower(cmd) != 0);
        char upper = (char)toupper(cmd);
        PathSegment seg;
        seg.relative = rel;

        switch(upper) {
        case 'M':
            seg.cmd=PathCmd::MoveTo;
            seg.args[0]=nextFloat(p); seg.args[1]=nextFloat(p);
            out.push_back(seg);
            cmd = rel ? 'l' : 'L';
            break;
        case 'L':
            seg.cmd=PathCmd::LineTo;
            seg.args[0]=nextFloat(p); seg.args[1]=nextFloat(p);
            out.push_back(seg);
            break;
        case 'H':
            seg.cmd=PathCmd::HLineTo;
            seg.args[0]=nextFloat(p);
            out.push_back(seg);
            break;
        case 'V':
            seg.cmd=PathCmd::VLineTo;
            seg.args[0]=nextFloat(p);
            out.push_back(seg);
            break;
        case 'C':
            seg.cmd=PathCmd::CubicTo;
            for(int i=0;i<6;i++) seg.args[i]=nextFloat(p);
            out.push_back(seg);
            break;
        case 'S':
            seg.cmd=PathCmd::CubicTo;
            seg.args[0]=0; seg.args[1]=0;
            seg.args[2]=nextFloat(p); seg.args[3]=nextFloat(p);
            seg.args[4]=nextFloat(p); seg.args[5]=nextFloat(p);
            seg.args[6]=1.f;
            out.push_back(seg);
            break;
        case 'Q':
            seg.cmd=PathCmd::QuadTo;
            for(int i=0;i<4;i++) seg.args[i]=nextFloat(p);
            out.push_back(seg);
            break;
        case 'T':
            seg.cmd=PathCmd::QuadTo;
            seg.args[0]=0; seg.args[1]=0;
            seg.args[2]=nextFloat(p); seg.args[3]=nextFloat(p);
            seg.args[4]=1.f;
            out.push_back(seg);
            break;
        case 'A':
            seg.cmd=PathCmd::ArcTo;
            for(int i=0;i<7;i++) seg.args[i]=nextFloat(p);
            out.push_back(seg);
            break;
        case 'Z':
            seg.cmd=PathCmd::ClosePath;
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

static std::vector<std::array<float,2>> parsePoints(const std::string& s) {
    std::vector<std::array<float,2>> out;
    const char* p = s.c_str();
    while(*p) {
        while(*p && (isspace(*p)||*p==',')) ++p;
        if(!*p) break;
        char* e1; float x = strtof(p, &e1); if(e1==p){++p;continue;} p=e1;
        while(*p && (isspace(*p)||*p==',')) ++p;
        char* e2; float y = strtof(p, &e2); if(e2==p){++p;continue;} p=e2;
        out.push_back({x,y});
    }
    return out;
}

static float getFloat(const xml::Node& n, const char* attrName, float def=0.f) {
    if(auto* v = n.attr(attrName)) return parseLength(*v);
    return def;
}

static void collectShapes(const xml::Node& node,
                          const Mat3& parentTf,
                          const Style& parentStyle,
                          const StyleSheet& sheet,
                          std::vector<SVGShape>& out)
{
    if(isHidden(node)) {
        SVGLOG("  skipping hidden node '%s'", node.tag.c_str());
        return;
    }

    Style  nodeStyle = parseStyle(node, parentStyle, sheet);
    Mat3   nodeTf    = parentTf;
    if(auto* t = node.attr("transform"))
        nodeTf = parentTf * parseTransform(*t);

    auto stamp = [&](SVGShape& s){ s.style=nodeStyle; s.transform=nodeTf; };

    if(node.tag=="rect") {
        SVGShape s; stamp(s); s.kind=ShapeKind::Rect;
        s.x=getFloat(node,"x"); s.y=getFloat(node,"y");
        s.width=getFloat(node,"width"); s.height=getFloat(node,"height");
        s.rx=getFloat(node,"rx"); s.ry=getFloat(node,"ry");
        if(s.rx>0&&s.ry==0) s.ry=s.rx;
        if(s.ry>0&&s.rx==0) s.rx=s.ry;
        SVGLOG("  rect x=%.1f y=%.1f w=%.1f h=%.1f fill_none=%d stroke_none=%d",
            s.x,s.y,s.width,s.height,s.style.fill.none,s.style.stroke.none);
        out.push_back(s);
    } else if(node.tag=="circle") {
        SVGShape s; stamp(s); s.kind=ShapeKind::Circle;
        s.cx=getFloat(node,"cx"); s.cy=getFloat(node,"cy"); s.r=getFloat(node,"r");
        SVGLOG("  circle cx=%.1f cy=%.1f r=%.1f", s.cx,s.cy,s.r);
        out.push_back(s);
    } else if(node.tag=="ellipse") {
        SVGShape s; stamp(s); s.kind=ShapeKind::Ellipse;
        s.cx=getFloat(node,"cx"); s.cy=getFloat(node,"cy");
        s.rx=getFloat(node,"rx"); s.ry=getFloat(node,"ry");
        SVGLOG("  ellipse cx=%.1f cy=%.1f rx=%.1f ry=%.1f", s.cx,s.cy,s.rx,s.ry);
        out.push_back(s);
    } else if(node.tag=="line") {
        SVGShape s; stamp(s); s.kind=ShapeKind::Line;
        s.x1=getFloat(node,"x1"); s.y1=getFloat(node,"y1");
        s.x2=getFloat(node,"x2"); s.y2=getFloat(node,"y2");
        SVGLOG("  line (%.1f,%.1f)→(%.1f,%.1f)", s.x1,s.y1,s.x2,s.y2);
        out.push_back(s);
    } else if(node.tag=="polyline") {
        SVGShape s; stamp(s); s.kind=ShapeKind::Polyline;
        if(auto* pv=node.attr("points")) s.points=parsePoints(*pv);
        SVGLOG("  polyline pts=%d", (int)s.points.size());
        out.push_back(s);
    } else if(node.tag=="polygon") {
        SVGShape s; stamp(s); s.kind=ShapeKind::Polygon;
        if(auto* pv=node.attr("points")) s.points=parsePoints(*pv);
        SVGLOG("  polygon pts=%d", (int)s.points.size());
        out.push_back(s);
    } else if(node.tag=="path") {
        SVGShape s; stamp(s); s.kind=ShapeKind::Path;
        if(auto* dv=node.attr("d")) s.path=parsePath(*dv);
        SVGLOG("  path cmds=%d", (int)s.path.size());
        out.push_back(s);
    }

    if(node.tag != "defs" && node.tag != "symbol") {
        for(auto& child : node.children)
            collectShapes(child, nodeTf, nodeStyle, sheet, out);
    }
}

SVGDocument parseSVG(const std::string& svg) {
    auto startTime = std::chrono::high_resolution_clock::now();

    SVGDocument doc;
    xml::Node root = xml::parse(svg);

    const xml::Node* svgNode = nullptr;
    std::vector<const xml::Node*> queue = {&root};
    while(!queue.empty() && !svgNode) {
        const xml::Node* cur = queue.back(); queue.pop_back();
        if(cur->tag == "svg") { svgNode = cur; break; }
        for(auto& c : cur->children) queue.push_back(&c);
    }
    if(!svgNode) {
        SVGLOG("ERROR: no <svg> element found");
        return doc;
    }

    float vbX=0, vbY=0, vbW=0, vbH=0;
    bool hasViewBox = false;
    if(auto* vb = svgNode->attr("viewBox"))
        hasViewBox = parseViewBox(*vb, vbX, vbY, vbW, vbH);

    float vw=0, vh=0;
    {
        const std::string* ws = svgNode->attr("width");
        const std::string* hs = svgNode->attr("height");
        float vbBase = hasViewBox ? vbW : 0.f;
        float vhBase = hasViewBox ? vbH : 0.f;
        vw = ws ? parseLength(*ws, vbBase) : (hasViewBox ? vbW : 800.f);
        vh = hs ? parseLength(*hs, vhBase) : (hasViewBox ? vbH : 600.f);
        if(vw <= 0) vw = hasViewBox ? vbW : 800.f;
        if(vh <= 0) vh = hasViewBox ? vbH : 600.f;
    }

    doc.viewport.vw = vw;
    doc.viewport.vh = vh;

    if(hasViewBox) {
        doc.viewport.x = vbX;
        doc.viewport.y = vbY;
        doc.viewport.w = vbW;
        doc.viewport.h = vbH;
    } else {
        doc.viewport.x = 0; doc.viewport.y = 0;
        doc.viewport.w = vw; doc.viewport.h = vh;
    }

    SVGLOG("SVG viewport: vw=%.0f vh=%.0f  viewBox=(%.0f,%.0f,%.0f,%.0f)",
        doc.viewport.vw, doc.viewport.vh,
        doc.viewport.x, doc.viewport.y, doc.viewport.w, doc.viewport.h);

    StyleSheet sheet;
    {
        const char* p = svg.c_str();
        while(*p) {
            const char* found = nullptr;
            for(const char* q = p; *q; ++q) {
                if((q[0]=='<') &&
                   (q[1]=='s'||q[1]=='S') &&
                   (q[2]=='t'||q[2]=='T') &&
                   (q[3]=='y'||q[3]=='Y') &&
                   (q[4]=='l'||q[4]=='L') &&
                   (q[5]=='e'||q[5]=='E') &&
                   ((unsigned char)q[6] <= 32 || q[6]=='>'))
                { found = q; break; }
            }
            if(!found) break;
            const char* inner = found + 6;
            while(*inner && *inner != '>') ++inner;
            if(*inner == '>') ++inner;
            const char* end = inner;
            while(*end) {
                if(end[0]=='<' && end[1]=='/') {
                    const char* e2 = end + 2;
                    while(*e2 == ' ') ++e2;
                    if((e2[0]=='s'||e2[0]=='S') &&
                       (e2[1]=='t'||e2[1]=='T') &&
                       (e2[2]=='y'||e2[2]=='Y') &&
                       (e2[3]=='l'||e2[3]=='L') &&
                       (e2[4]=='e'||e2[4]=='E'))
                        break;
                }
                ++end;
            }
            std::string cssText(inner, end);
            SVGLOG("Found <style> block (%d chars)", (int)cssText.size());
            parseStyleSheet(cssText, sheet);
            p = (*end ? end + 1 : end);
        }
    }
    SVGLOG("Stylesheet: %d rules", (int)sheet.size());

    Style defaultStyle;
    defaultStyle.fill   = Paint::solid({0,0,0,1});
    defaultStyle.stroke = Paint::transparent();
    defaultStyle.strokeWidth = 1.f;

    collectShapes(*svgNode, Mat3::identity(), defaultStyle, sheet, doc.shapes);
    auto endTime = std::chrono::high_resolution_clock::now();
    float ms =  std::chrono::duration<float, std::milli>(endTime - startTime).count();
    SVGLOG("SVG parsed: %d shapes in %.2f ms", (int)doc.shapes.size(), ms);
    return doc;
}