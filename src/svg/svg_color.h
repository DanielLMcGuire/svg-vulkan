#pragma once
#include "svg_types.h"
#include <string>

float parseLength(const char *s, float percentOf = 0.f);
float parseLength(const std::string &s, float percentOf = 0.f);
bool parseViewBox(const std::string &s, float &x, float &y, float &w, float &h);
Color parseColor(const std::string &s);
