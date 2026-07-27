#pragma once
#include "tessellator_internal.h"
#include <cstdint>
#include <vector>

namespace earclip
{

std::vector<uint32_t> triangulate(const std::vector<V2> &polyIn);

} // namespace earclip
