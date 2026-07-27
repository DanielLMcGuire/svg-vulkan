#pragma once
#include "paint_eval.h"
#include "svg_types.h"
#include "tessellator.h"
#include "tessellator_internal.h"
#include <vector>

std::vector<V2> makeRoundedRect(float x, float y, float w, float h, float rx,
                                float ry, int arcSegs = 16);
std::vector<V2> makeEllipse(float cx, float cy, float rx, float ry,
                            int segs = 64);

void strokeWithDash(Mesh &out, const std::vector<V2> &pts, bool closed,
                    const Style &st, const PaintEval &paint);
