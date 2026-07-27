#pragma once
#include "svg_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vfont {

bool available();

float appendGlyphPath(std::vector<PathSegment>& out, uint32_t codepoint,
                      float penX, float penY, float fontSize);

float glyphAdvance(uint32_t codepoint, float fontSize);

float kernAdvance(uint32_t cp1, uint32_t cp2, float fontSize);

uint32_t utf8Decode(const std::string& s, size_t& i);

} // namespace vfont
