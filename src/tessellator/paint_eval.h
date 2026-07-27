#pragma once
#include "svg_types.h"
#include <functional>
#include <string>
#include <unordered_map>

using PaintEval = std::function<Color(float, float)>;

PaintEval
makePaintEval(const Paint &paint, const Style &style,
              const std::unordered_map<std::string, GradientDef> *gradients,
              float bboxMinX, float bboxMinY, float bboxW, float bboxH,
              float opacityMul);

bool paintIsRadialGradient(
    const Paint &paint,
    const std::unordered_map<std::string, GradientDef> *gradients);
