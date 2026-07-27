#pragma once
#include <string>
#include <vector>

struct CliOptions
{
    std::string svgPath;
    std::string ppmPath;
    int rasterWidth = 800;
    int rasterHeight = 600;
    bool wantRaster = false;
};

std::vector<std::string> splitCommandLine(const std::string &cmd);

bool parseSizeArg(const std::string &s, int &outW, int &outH);

CliOptions parseArgs(const std::vector<std::string> &tokens);
