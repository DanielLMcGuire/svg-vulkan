#pragma once
#include "svg_types.h"
#include <array>
#include <string>
#include <vector>

std::vector<PathSegment> parsePath(const std::string &d);
std::vector<std::array<float, 2>> parsePoints(const std::string &s);
