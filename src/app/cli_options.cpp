#include "cli_options.h"
#include <cctype>
#include <stdexcept>

std::vector<std::string> splitCommandLine(const std::string &cmd)
{
    std::vector<std::string> tokens;
    std::string cur;
    bool inQuotes = false;
    for (char c : cmd)
    {
        if (c == '"')
        {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && std::isspace(static_cast<unsigned char>(c)))
        {
            if (!cur.empty())
            {
                tokens.push_back(cur);
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        tokens.push_back(cur);
    return tokens;
}

bool parseSizeArg(const std::string &s, int &outW, int &outH)
{
    size_t xpos = s.find_first_of("xX");
    if (xpos == std::string::npos || xpos == 0 || xpos + 1 >= s.size())
        return false;
    try
    {
        int w = std::stoi(s.substr(0, xpos));
        int h = std::stoi(s.substr(xpos + 1));
        if (w <= 0 || h <= 0)
            return false;
        outW = w;
        outH = h;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

CliOptions parseArgs(const std::vector<std::string> &tokens)
{
    CliOptions opts;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const std::string &tok = tokens[i];
        if (tok == "--ppm" && i + 1 < tokens.size())
        {
            opts.ppmPath = tokens[++i];
            opts.wantRaster = true;
        }
        else if (tok.rfind("--ppm=", 0) == 0)
        {
            opts.ppmPath = tok.substr(6);
            opts.wantRaster = true;
        }
        else if (tok == "--size" && i + 1 < tokens.size())
        {
            int w, h;
            if (parseSizeArg(tokens[++i], w, h))
            {
                opts.rasterWidth = w;
                opts.rasterHeight = h;
            }
        }
        else if (tok.rfind("--size=", 0) == 0)
        {
            int w, h;
            if (parseSizeArg(tok.substr(7), w, h))
            {
                opts.rasterWidth = w;
                opts.rasterHeight = h;
            }
        }
        else if (opts.svgPath.empty() && tok.rfind("--", 0) != 0)
        {
            opts.svgPath = tok;
        }
    }
    return opts;
}
