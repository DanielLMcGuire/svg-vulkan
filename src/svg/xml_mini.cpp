#include "xml_mini.h"

namespace xml
{

static void skipWS(const char *&p)
{
    while (*p && (unsigned char)*p <= 32)
        ++p;
}

static std::string parseName(const char *&p)
{
    std::string out;
    while (*p && *p != '=' && *p != '>' && *p != '/' && (unsigned char)*p > 32)
        out += *p++;
    return out;
}

static std::string parseAttrValue(const char *&p)
{
    skipWS(p);
    if (*p != '=')
        return {};
    ++p;
    skipWS(p);
    if (*p != '"' && *p != '\'')
        return {};
    char q = *p++;
    std::string out;
    while (*p && *p != q)
    {
        if (*p == '&')
        {
            ++p;
            std::string ent;
            while (*p && *p != ';')
                ent += *p++;
            if (*p)
                ++p;
            if (ent == "lt")
                out += '<';
            else if (ent == "gt")
                out += '>';
            else if (ent == "amp")
                out += '&';
            else if (ent == "quot")
                out += '"';
            else if (ent == "apos")
                out += '\'';
        }
        else
        {
            out += *p++;
        }
    }
    if (*p)
        ++p;
    return out;
}

static Node parseNode(const char *&p);

static void parseChildren(const char *&p, Node &parent)
{
    while (*p)
    {
        skipWS(p);
        if (!*p)
            break;
        if (p[0] == '<' && p[1] == '/')
            break;

        if (p[0] == '<' && p[1] == '!')
        {
            if (p[2] == '-' && p[3] == '-')
            {
                p += 4;
                while (*p && !(*p == '-' && p[1] == '-' && p[2] == '>'))
                    ++p;
                if (*p)
                    p += 3;
            }
            else
            {
                while (*p && *p != '>')
                    ++p;
                if (*p)
                    ++p;
            }
            continue;
        }
        if (*p == '<')
        {
            parent.children.push_back(parseNode(p));
        }
        else
        {
            while (*p && *p != '<')
            {
                if (*p == '&')
                {
                    ++p;
                    std::string ent;
                    while (*p && *p != ';')
                        ent += *p++;
                    if (*p)
                        ++p;
                    if (ent == "lt")
                        parent.text += '<';
                    else if (ent == "gt")
                        parent.text += '>';
                    else if (ent == "amp")
                        parent.text += '&';
                    else if (ent == "quot")
                        parent.text += '"';
                    else if (ent == "apos")
                        parent.text += '\'';
                }
                else
                {
                    parent.text += *p++;
                }
            }
        }
    }
}

static Node parseNode(const char *&p)
{
    Node node;
    if (*p == '<')
        ++p;
    if (*p == '?')
    {
        while (*p && *p != '>')
            ++p;
        if (*p)
            ++p;
        return node;
    }
    if (p[0] == '!' && p[1] == '-')
    {
        while (*p && !(*p == '-' && p[1] == '-' && p[2] == '>'))
            ++p;
        if (*p)
            p += 3;
        return node;
    }

    node.tag = parseName(p);
    if (auto c = node.tag.rfind(':'); c != std::string::npos)
        node.tag = node.tag.substr(c + 1);

    while (*p && *p != '>' && *p != '/')
    {
        skipWS(p);
        if (*p == '>' || *p == '/' || !*p)
            break;
        Attr a;
        a.name = parseName(p);
        if (a.name.empty())
        {
            ++p;
            continue;
        }

        if (auto c = a.name.rfind(':'); c != std::string::npos)
            a.name = a.name.substr(c + 1);
        a.value = parseAttrValue(p);
        if (!a.name.empty())
            node.attrs.push_back(std::move(a));
    }
    if (*p == '/')
    {
        ++p;
        if (*p == '>')
            ++p;
        return node;
    }
    if (*p == '>')
        ++p;

    parseChildren(p, node);

    if (p[0] == '<' && p[1] == '/')
    {
        p += 2;
        while (*p && *p != '>')
            ++p;
        if (*p)
            ++p;
    }
    return node;
}

Node parse(const std::string &src)
{
    const char *p = src.c_str();
    skipWS(p);
    while (*p == '<' && (p[1] == '?' || p[1] == '!'))
    {
        if (p[1] == '!' && p[2] == '-' && p[3] == '-')
        {
            p += 4;
            while (*p && !(*p == '-' && p[1] == '-' && p[2] == '>'))
                ++p;
            if (*p)
                p += 3;
        }
        else
        {
            while (*p && *p != '>')
                ++p;
            if (*p)
                ++p;
        }
        skipWS(p);
    }
    return parseNode(p);
}

} // namespace xml
