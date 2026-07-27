#pragma once
#include "paint_eval.h"
#include "tessellator.h"
#include "tessellator_internal.h"
#include <vector>

void computeBounds(const std::vector<V2> &pts, float &minX, float &minY,
                   float &maxX, float &maxY);

void appendFillRadial(Mesh &mesh, const std::vector<V2> &ring, V2 center,
                      const PaintEval &paint, int numRings = 10);

void appendFill(Mesh &mesh, const std::vector<V2> &poly,
                const PaintEval &paint);

void appendTriangleStrip(Mesh &mesh, const std::vector<V2> &tris,
                         const PaintEval &paint);

void buildFan(Mesh::StencilFill &sf, const std::vector<V2> &pts);
