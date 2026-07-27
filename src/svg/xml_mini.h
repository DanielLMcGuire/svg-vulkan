#pragma once
#include <string>
#include <vector>

namespace xml
{

struct Attr
{
    std::string name, value;
};

struct Node
{
    std::string tag;
    std::string text;
    std::vector<Attr> attrs;
    std::vector<Node> children;

    const std::string *attr(const char *name) const
    {
        for (auto &a : attrs)
            if (a.name == name)
                return &a.value;
        return nullptr;
    }
};

Node parse(const std::string &src);

} // namespace xml
