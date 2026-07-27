#include "svg_style.h"
#include "svg_color.h"
#include "svg_parser_internal.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

void trimStr(std::string &t)
{
    size_t a = t.find_first_not_of(" \t\r\n");
    size_t b = t.find_last_not_of(" \t\r\n");
    t = (a == std::string::npos) ? "" : t.substr(a, b - a + 1);
}

void parseStyleSheet(const std::string &css, StyleSheet &sheet)
{
    std::string src;
    src.reserve(css.size());
    for (size_t i = 0; i < css.size();)
    {
        if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/'))
                ++i;
            i += 2;
        }
        else
        {
            src += css[i++];
        }
    }

    size_t pos = 0;
    while (pos < src.size())
    {
        size_t lbrace = src.find('{', pos);
        if (lbrace == std::string::npos)
            break;
        size_t rbrace = src.find('}', lbrace + 1);
        if (rbrace == std::string::npos)
            break;

        std::string selectorBlock = src.substr(pos, lbrace - pos);
        std::string declarations = src.substr(lbrace + 1, rbrace - lbrace - 1);
        trimStr(declarations);
        pos = rbrace + 1;

        trimStr(selectorBlock);
        if (selectorBlock.empty() || selectorBlock[0] == '@')
            continue;

        std::istringstream selStream(selectorBlock);
        std::string sel;
        while (std::getline(selStream, sel, ','))
        {
            trimStr(sel);
            if (sel.empty())
                continue;

            if (sel.find(' ') != std::string::npos)
                continue;
            SVGLOG("CSS rule: '%s' => '%s'", sel.c_str(), declarations.c_str());

            auto it = sheet.find(sel);
            if (it == sheet.end())
                sheet[sel] = declarations;
            else
            {
                it->second += ';';
                it->second += declarations;
            }
        }
    }
}

Paint parsePaint(const std::string &val)
{
    if (val == "none")
        return Paint::transparent();
    if (val == "currentColor")
        return Paint::currentColor();
    if (val.rfind("url(", 0) == 0)
    {
        auto rp = val.find(')');
        std::string inner =
            val.substr(4, rp == std::string::npos ? std::string::npos : rp - 4);
        trimStr(inner);
        if (!inner.empty() && inner.front() == '#')
            inner.erase(inner.begin());
        if (!inner.empty() && (inner.front() == '\'' || inner.front() == '"'))
            inner.erase(inner.begin());
        if (!inner.empty() && (inner.back() == '\'' || inner.back() == '"'))
            inner.pop_back();

        Color fallback = {0, 0, 0, 1};
        if (rp != std::string::npos && rp + 1 < val.size())
        {
            std::string rest = val.substr(rp + 1);
            trimStr(rest);
            if (!rest.empty() && rest != "none")
                fallback = parseColor(rest);
        }
        return Paint::gradient(inner, fallback);
    }
    return Paint::solid(parseColor(val));
}

void applyDeclarations(const std::string &decls, Style &s)
{
    std::istringstream ss(decls);
    std::string token;
    while (std::getline(ss, token, ';'))
    {
        auto colon = token.find(':');
        if (colon == std::string::npos)
            continue;
        std::string key = token.substr(0, colon);
        std::string val = token.substr(colon + 1);
        trimStr(key);
        trimStr(val);
        if (val == "inherit" || key.empty() || val.empty())
            continue;
        if (key == "isolation" || key == "mix-blend-mode" ||
            key == "enable-background")
            continue;
        if (key == "fill")
        {
            s.fill = parsePaint(val);
        }
        else if (key == "stroke")
        {
            s.stroke = parsePaint(val);
        }
        else if (key == "stroke-width")
            s.strokeWidth = parseLength(val);
        else if (key == "opacity")
            s.opacity = strtof(val.c_str(), nullptr);
        else if (key == "fill-opacity")
            s.fillOpacity = strtof(val.c_str(), nullptr);
        else if (key == "stroke-opacity")
            s.strokeOpacity = strtof(val.c_str(), nullptr);
        else if (key == "color")
            s.currentColor = parseColor(val);
        else if (key == "font-size")
            s.fontSize = parseLength(val, s.fontSize);
        else if (key == "text-anchor")
            s.textAnchor = val;
        else if (key == "stroke-linecap")
        {
            if (val == "round")
                s.lineCap = LineCap::Round;
            else if (val == "square")
                s.lineCap = LineCap::Square;
            else
                s.lineCap = LineCap::Butt;
        }
        else if (key == "stroke-linejoin")
        {
            if (val == "round")
                s.lineJoin = LineJoin::Round;
            else if (val == "bevel")
                s.lineJoin = LineJoin::Bevel;
            else
                s.lineJoin = LineJoin::Miter;
        }
        else if (key == "stroke-miterlimit")
        {
            s.miterLimit = strtof(val.c_str(), nullptr);
        }
        else if (key == "stroke-dasharray")
        {
            s.dashArray.clear();
            if (val != "none")
            {
                const char *p = val.c_str();
                while (*p)
                {
                    while (*p && (isspace((unsigned char)*p) || *p == ','))
                        ++p;
                    if (!*p)
                        break;
                    char *e;
                    float v = strtof(p, &e);
                    if (e == p)
                    {
                        ++p;
                        continue;
                    }
                    if (v >= 0.f)
                        s.dashArray.push_back(v);
                    p = e;
                }
                if (!s.dashArray.empty() && s.dashArray.size() % 2 == 1)
                {
                    auto copy = s.dashArray;
                    s.dashArray.insert(s.dashArray.end(), copy.begin(),
                                       copy.end());
                }
            }
        }
        else if (key == "stroke-dashoffset")
        {
            s.dashOffset = parseLength(val);
        }
        else if (key == "fill-rule")
        {
            s.fillRule =
                (val == "evenodd") ? FillRule::EvenOdd : FillRule::NonZero;
        }
        else if (key == "display")
        {
            s.display = (val != "none");
        }
        else if (key == "visibility")
        {
            s.visible = !(val == "hidden" || val == "collapse");
        }
    }
}

Style parseStyle(const xml::Node &node, const Style &parent,
                 const StyleSheet &sheet)
{
    Style s = parent;
    s.opacity = 1.f;

    for (auto &attr : node.attrs)
    {
        std::string decl = attr.name + ':' + attr.value;
        applyDeclarations(decl, s);
    }

    {
        auto it = sheet.find(node.tag);
        if (it != sheet.end())
            applyDeclarations(it->second, s);
    }
    if (auto *classAttr = node.attr("class"))
    {
        std::istringstream cs(*classAttr);
        std::string cls;
        while (cs >> cls)
        {
            auto it = sheet.find('.' + cls);
            if (it != sheet.end())
            {
                SVGLOG("applying CSS class .%s", cls.c_str());
                applyDeclarations(it->second, s);
            }
        }
    }
    if (auto *idAttr = node.attr("id"))
    {
        auto it = sheet.find('#' + *idAttr);
        if (it != sheet.end())
            applyDeclarations(it->second, s);
    }

    if (auto *styleAttr = node.attr("style"))
        applyDeclarations(*styleAttr, s);

    s.opacity *= parent.opacity;
    return s;
}
