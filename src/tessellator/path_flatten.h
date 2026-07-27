#pragma once
#include "svg_types.h"
#include "tessellator_internal.h"
#include <vector>

struct Contour
{
    std::vector<V2> pts;
    bool closed = false;
};

std::vector<Contour> pathToContours(const std::vector<PathSegment> &segs);
