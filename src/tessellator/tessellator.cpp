#include "tessellator.h"
#include "fill.h"
#include "paint_eval.h"
#include "path_flatten.h"
#include "stroke.h"
#include "tessellator_internal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

static void
tessellateShape(const SVGShape &shape, Mesh &out,
                const std::unordered_map<std::string, GradientDef> *gradients)
{
    const Style &st = shape.style;
    const Mat3 &tf = shape.transform;

    float globalA = st.opacity;
    auto applyTf = [&](std::vector<V2> &pts)
    {
        for (auto &p : pts)
        {
            auto r = tf.apply(p[0], p[1]);
            p = {r[0], r[1]};
        }
    };
    auto makeFillPaint = [&](float bx0, float by0, float bw, float bh)
    {
        return makePaintEval(st.fill, st, gradients, bx0, by0, bw, bh,
                             st.fillOpacity * globalA);
    };
    auto makeStrokePaint = [&](float bx0, float by0, float bw, float bh)
    {
        return makePaintEval(st.stroke, st, gradients, bx0, by0, bw, bh,
                             st.strokeOpacity * globalA);
    };

#ifdef _DEBUG
    TESSLOG("shape kind=%d  fill_none=%d fill_grad=%d  stroke_none=%d sw=%.1f "
            "stroke_grad=%d  opacity=%.2f",
            (int)shape.kind, st.fill.none, st.fill.isGradient(), st.stroke.none,
            st.strokeWidth, st.stroke.isGradient(), globalA);
#endif

    switch (shape.kind)
    {
    case ShapeKind::Rect:
    {
        std::vector<V2> poly;
        if (shape.rx > 0 || shape.ry > 0)
            poly = makeRoundedRect(shape.x, shape.y, shape.width, shape.height,
                                   shape.rx, shape.ry);
        else
            poly = {{shape.x, shape.y},
                    {shape.x + shape.width, shape.y},
                    {shape.x + shape.width, shape.y + shape.height},
                    {shape.x, shape.y + shape.height}};
        applyTf(poly);
        float bx, by, bw, bh;
        computeBounds(poly, bx, by, bw, bh);
        bw -= bx;
        bh -= by;
        if (!st.fill.none)
        {
            PaintEval fp = makeFillPaint(bx, by, bw, bh);
            if (paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, poly, {bx + bw * 0.5f, by + bh * 0.5f},
                                 fp);
            else
                appendFill(out, poly, fp);
        }
        if (!st.stroke.none)
            strokeWithDash(out, poly, true, st,
                           makeStrokePaint(bx, by, bw, bh));
        break;
    }
    case ShapeKind::Circle:
    {
        auto poly = makeEllipse(shape.cx, shape.cy, shape.r, shape.r);
        applyTf(poly);
        float bx, by, bw, bh;
        computeBounds(poly, bx, by, bw, bh);
        bw -= bx;
        bh -= by;
        if (!st.fill.none)
        {
            PaintEval fp = makeFillPaint(bx, by, bw, bh);
            if (paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, poly, {bx + bw * 0.5f, by + bh * 0.5f},
                                 fp);
            else
                appendFill(out, poly, fp);
        }
        if (!st.stroke.none)
            strokeWithDash(out, poly, true, st,
                           makeStrokePaint(bx, by, bw, bh));
        break;
    }
    case ShapeKind::Ellipse:
    {
        auto poly = makeEllipse(shape.cx, shape.cy, shape.rx, shape.ry);
        applyTf(poly);
        float bx, by, bw, bh;
        computeBounds(poly, bx, by, bw, bh);
        bw -= bx;
        bh -= by;
        if (!st.fill.none)
        {
            PaintEval fp = makeFillPaint(bx, by, bw, bh);
            if (paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, poly, {bx + bw * 0.5f, by + bh * 0.5f},
                                 fp);
            else
                appendFill(out, poly, fp);
        }
        if (!st.stroke.none)
            strokeWithDash(out, poly, true, st,
                           makeStrokePaint(bx, by, bw, bh));
        break;
    }
    case ShapeKind::Line:
    {
        if (!st.stroke.none)
        {
            std::vector<V2> ln = {{shape.x1, shape.y1}, {shape.x2, shape.y2}};
            applyTf(ln);
            float bx, by, bw, bh;
            computeBounds(ln, bx, by, bw, bh);
            bw -= bx;
            bh -= by;
            strokeWithDash(out, ln, false, st, makeStrokePaint(bx, by, bw, bh));
        }
        break;
    }
    case ShapeKind::Polyline:
    {
        auto pts = shape.points;
        applyTf(pts);
        float bx, by, bw, bh;
        computeBounds(pts, bx, by, bw, bh);
        bw -= bx;
        bh -= by;
        if (!st.fill.none && pts.size() >= 3)
        {
            PaintEval fp = makeFillPaint(bx, by, bw, bh);
            if (paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, pts, {bx + bw * 0.5f, by + bh * 0.5f},
                                 fp);
            else
                appendFill(out, pts, fp);
        }
        if (!st.stroke.none)
            strokeWithDash(out, pts, false, st,
                           makeStrokePaint(bx, by, bw, bh));
        break;
    }
    case ShapeKind::Polygon:
    {
        auto pts = shape.points;
        applyTf(pts);
        float bx, by, bw, bh;
        computeBounds(pts, bx, by, bw, bh);
        bw -= bx;
        bh -= by;
        if (!st.fill.none && pts.size() >= 3)
        {
            PaintEval fp = makeFillPaint(bx, by, bw, bh);
            if (paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, pts, {bx + bw * 0.5f, by + bh * 0.5f},
                                 fp);
            else
                appendFill(out, pts, fp);
        }
        if (!st.stroke.none)
            strokeWithDash(out, pts, true, st, makeStrokePaint(bx, by, bw, bh));
        break;
    }
    case ShapeKind::Path:
    {
        auto rawContours = pathToContours(shape.path);
        std::vector<std::vector<V2>> rings;
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (auto &c : rawContours)
        {
            auto pts = c.pts;
            applyTf(pts);
            if (pts.size() >= 2)
            {
                for (auto &p : pts)
                {
                    minX = std::min(minX, p[0]);
                    minY = std::min(minY, p[1]);
                    maxX = std::max(maxX, p[0]);
                    maxY = std::max(maxY, p[1]);
                }
            }
            rings.push_back(std::move(pts));
        }
        bool haveBounds = (minX <= maxX);
        float bw = haveBounds ? maxX - minX : 0.f,
              bh = haveBounds ? maxY - minY : 0.f;

        if (!st.fill.none)
        {
            std::vector<std::vector<V2>> fillRings;
            for (auto &r : rings)
                if (r.size() >= 3)
                    fillRings.push_back(r);

            if (fillRings.size() == 1)
            {
                PaintEval fp = makeFillPaint(minX, minY, bw, bh);
                if (paintIsRadialGradient(st.fill, gradients))
                    appendFillRadial(out, fillRings[0],
                                     {minX + bw * 0.5f, minY + bh * 0.5f}, fp);
                else
                    appendFill(out, fillRings[0], fp);
            }
            else if (fillRings.size() > 1)
            {
                Mesh::StencilFill sf;
                sf.evenOdd = (st.fillRule == FillRule::EvenOdd);

                for (auto &ring : fillRings)
                    buildFan(sf, ring);

                PaintEval paint = makeFillPaint(minX, minY, bw, bh);
                sf.bboxBase = (uint32_t)sf.verts.size();
                auto pushCorner = [&](float x, float y)
                {
                    Color c = paint(x, y);
                    float a = c.a;
                    sf.verts.push_back({x, y, c.r * a, c.g * a, c.b * a, a});
                };
                pushCorner(minX, minY);
                pushCorner(maxX, minY);
                pushCorner(maxX, maxY);
                pushCorner(minX, maxY);

                sf.indices.push_back(sf.bboxBase);
                sf.indices.push_back(sf.bboxBase + 1);
                sf.indices.push_back(sf.bboxBase + 2);
                sf.indices.push_back(sf.bboxBase + 2);
                sf.indices.push_back(sf.bboxBase + 3);
                sf.indices.push_back(sf.bboxBase);

                out.stencilFills.push_back(std::move(sf));
            }
        }

        if (!st.stroke.none)
        {
            PaintEval paint = makeStrokePaint(minX, minY, bw, bh);
            for (size_t i = 0; i < rawContours.size(); i++)
                strokeWithDash(out, rings[i], rawContours[i].closed, st, paint);
        }
        break;
    }
    }
}

Mesh tessellateDocument(const SVGDocument &doc)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    Mesh m;
    for (auto &shape : doc.shapes)
        tessellateShape(shape, m, &doc.gradients);

    auto endTime = std::chrono::high_resolution_clock::now();
    float ms =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();
    TESSLOG("Tessellation complete: %d vertices, %d indices (%d triangles) in "
            "%.2f ms",
            (int)m.vertices.size(), (int)m.indices.size(),
            (int)m.indices.size() / 3, ms);
    return m;
}