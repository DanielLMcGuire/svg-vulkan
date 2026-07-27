#include "paint_eval.h"
#include <algorithm>
#include <cmath>

static Color sampleGradientStops(const std::vector<GradientStop> &stops,
                                 float t, SpreadMethod spread)
{
    if (stops.empty())
        return {0, 0, 0, 0};
    switch (spread)
    {
    case SpreadMethod::Pad:
        t = std::max(0.f, std::min(1.f, t));
        break;
    case SpreadMethod::Repeat:
        t = t - floorf(t);
        break;
    case SpreadMethod::Reflect:
    {
        float m = fmodf(fabsf(t), 2.f);
        t = (m > 1.f) ? 2.f - m : m;
        break;
    }
    }
    if (stops.size() == 1)
        return stops[0].color;
    if (t <= stops.front().offset)
        return stops.front().color;
    if (t >= stops.back().offset)
        return stops.back().color;
    for (size_t i = 0; i + 1 < stops.size(); i++)
    {
        float o0 = stops[i].offset, o1 = stops[i + 1].offset;
        if (t >= o0 && t <= o1)
        {
            float f = (o1 > o0) ? (t - o0) / (o1 - o0) : 0.f;
            const Color &c0 = stops[i].color;
            const Color &c1 = stops[i + 1].color;
            return {c0.r + (c1.r - c0.r) * f, c0.g + (c1.g - c0.g) * f,
                    c0.b + (c1.b - c0.b) * f, c0.a + (c1.a - c0.a) * f};
        }
    }
    return stops.back().color;
}

PaintEval
makePaintEval(const Paint &paint, const Style &style,
              const std::unordered_map<std::string, GradientDef> *gradients,
              float bboxMinX, float bboxMinY, float bboxW, float bboxH,
              float opacityMul)
{
    if (paint.none)
        return [](float, float) { return Color::none(); };

    if (paint.isGradient() && gradients)
    {
        auto it = gradients->find(paint.gradientId);
        if (it != gradients->end() && !it->second.stops.empty())
        {
            const GradientDef *g = &it->second;
            float safeW = (fabsf(bboxW) < 1e-6f) ? 1.f : bboxW;
            float safeH = (fabsf(bboxH) < 1e-6f) ? 1.f : bboxH;
            return [g, bboxMinX, bboxMinY, safeW, safeH,
                    opacityMul](float px, float py) -> Color
            {
                float nx, ny;
                if (g->userSpaceOnUse)
                {
                    nx = px;
                    ny = py;
                }
                else
                {
                    nx = (px - bboxMinX) / safeW;
                    ny = (py - bboxMinY) / safeH;
                }
                auto gp = g->gradientTransform.apply(nx, ny);

                float t;
                if (g->kind == GradientKind::Linear)
                {
                    float dx = g->x2 - g->x1, dy = g->y2 - g->y1;
                    float len2 = dx * dx + dy * dy;
                    t = (len2 < 1e-12f)
                            ? 0.f
                            : ((gp[0] - g->x1) * dx + (gp[1] - g->y1) * dy) /
                                  len2;
                }
                else
                {
                    float pfx = gp[0] - g->fx, pfy = gp[1] - g->fy;
                    float cfx = g->fx - g->cx, cfy = g->fy - g->cy;
                    float a = pfx * pfx + pfy * pfy;
                    if (a < 1e-12f)
                        t = 0.f;
                    else
                    {
                        float b = 2.f * (cfx * pfx + cfy * pfy);
                        float c = cfx * cfx + cfy * cfy - g->r * g->r;
                        float disc = std::max(0.f, b * b - 4.f * a * c);
                        float ts = (-b + sqrtf(disc)) / (2.f * a);
                        t = (ts > 1e-6f) ? 1.f / ts : 0.f;
                    }
                }
                Color c = sampleGradientStops(g->stops, t, g->spread);
                c.a *= opacityMul;
                return c;
            };
        }
    }

    Color base = paint.useCurrentColor ? style.currentColor : paint.color;
    base.a *= opacityMul;
    return [base](float, float) { return base; };
}

bool paintIsRadialGradient(
    const Paint &paint,
    const std::unordered_map<std::string, GradientDef> *gradients)
{
    if (!paint.isGradient() || !gradients)
        return false;
    auto it = gradients->find(paint.gradientId);
    return it != gradients->end() && it->second.kind == GradientKind::Radial &&
           !it->second.stops.empty();
}
