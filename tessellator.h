#pragma once
#include "svg_types.h"
#include <vector>

struct Mesh {
    struct Vertex {
        float x, y;
        float r, g, b, a;   // premultiplied alpha
    };
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    struct StencilFill {
        std::vector<Vertex>   verts;
        std::vector<uint32_t> indices;   // [fan tris ... | 6 bbox indices]
        uint32_t              bboxBase;  // index into verts[] of first bbox vertex
        bool                  evenOdd;
    };
    std::vector<StencilFill> stencilFills;
};

Mesh tessellateDocument(const SVGDocument& doc);

static constexpr float CURVE_TOL = 0.023f;
static constexpr int   ARC_SEGS  = 64;
