#include "path_flatten.h"
#include "tessellator.h"
#include <algorithm>
#include <cmath>

static void flattenCubic(std::vector<V2> &pts, V2 p0, V2 p1, V2 p2, V2 p3,
                         int depth = 0)
{
    if (depth > 8)
    {
        pts.push_back(p3);
        return;
    }

    // calc deviation vectors from the baseline chord
    float ux = 3.0f * p1[0] - 2.0f * p0[0] - p3[0];
    float uy = 3.0f * p1[1] - 2.0f * p0[1] - p3[1];
    float vx = 3.0f * p2[0] - p0[0] - 2.0f * p3[0];
    float vy = 3.0f * p2[1] - p0[1] - 2.0f * p3[1];

    // max squared deviation
    float max_dev_sq = std::max(ux * ux + uy * uy, vx * vx + vy * vy);

    // flatness check: max error is 1/4 of dev vector.
    // squared check: dev^2 <= 16 * TOL^2
    if (max_dev_sq <= 16.0f * CURVE_TOL * CURVE_TOL)
    {
        pts.push_back(p3);
        return;
    }

    // De Casteljau algo for splitting of a cubic Bézier curve at t=0.5:
    auto mid = [](V2 a, V2 b)
    { return V2{(a[0] + b[0]) * .5f, (a[1] + b[1]) * .5f}; };

    V2 m01 = mid(p0, p1), m12 = mid(p1, p2), m23 = mid(p2, p3);
    V2 m012 = mid(m01, m12), m123 = mid(m12, m23);
    V2 m0123 = mid(m012, m123);

    flattenCubic(pts, p0, m01, m012, m0123, depth + 1);
    flattenCubic(pts, m0123, m123, m23, p3, depth + 1);
}

static void flattenQuad(std::vector<V2> &pts, V2 p0, V2 p1, V2 p2,
                        int depth = 0)
{
    if (depth > 8)
    {
        pts.push_back(p2);
        return;
    }

    // quadratic deviation vector
    float ux = 2.0f * p1[0] - p0[0] - p2[0];
    float uy = 2.0f * p1[1] - p0[1] - p2[1];

    // flatness check: dev^2 <= 16 * TOL^2
    if ((ux * ux + uy * uy) <= 16.0f * CURVE_TOL * CURVE_TOL)
    {
        pts.push_back(p2);
        return;
    }

    // De Casteljau
    auto mid = [](V2 a, V2 b)
    { return V2{(a[0] + b[0]) * .5f, (a[1] + b[1]) * .5f}; };

    V2 m01 = mid(p0, p1), m12 = mid(p1, p2);
    V2 m0112 = mid(m01, m12);

    flattenQuad(pts, p0, m01, m0112, depth + 1);
    flattenQuad(pts, m0112, m12, p2, depth + 1);
}

static void flattenArc(std::vector<V2> &pts, V2 cur, float rx, float ry,
                       float xRot, bool largeArc, bool sweep, V2 end)
{
    if (rx == 0 || ry == 0)
    {
        pts.push_back(end);
        return;
    }

    // convert rotation to radians: φ = xRot * (π / 180)
    float phi = xRot * PI / 180.f;
    float cphi = cosf(phi), sphi = sinf(phi);

    // relative half-distance from end to start: Δx = (x1 - x2)/2, Δy = (y1 -
    // y2)/2
    float dx2 = (cur[0] - end[0]) * .5f, dy2 = (cur[1] - end[1]) * .5f;

    // rotate half-distance into ellipse local space:
    // x1' = Δx*cos(φ) + Δy*sin(φ)
    // y1' = -Δx*sin(φ) + Δy*cos(φ)
    float x1p = cphi * dx2 + sphi * dy2, y1p = -sphi * dx2 + cphi * dy2;

    rx = fabsf(rx);
    ry = fabsf(ry);

    // check if endpoints are within ellipse bounds: λ = (x1'/rx)² + (y1'/ry)²
    float lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
    if (lam > 1)
    {
        // scale radii if endpoints are too far: rx = rx * √λ, ry = ry * √λ
        float s = sqrtf(lam);
        rx *= s;
        ry *= s;
    }

    // calc center offset components:
    // num = (rx²*ry²) - (rx²*y1'²) - (ry²*x1'²)
    // den = (rx²*y1'²) + (ry²*x1'²)
    float num = (rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p);
    float den = (rx * rx * y1p * y1p + ry * ry * x1p * x1p);

    // solve for center scaling factor: sq = √(num / den)
    float sq = (den > 0) ? sqrtf(fabsf(num / den)) : 0;
    if (largeArc == sweep)
        sq = -sq;

    // local center coordinates: cx' = (sq * rx * y1') / ry, cy' = -(sq * ry *
    // x1') / rx
    float cxp = sq * rx * y1p / ry, cyp = -sq * ry * x1p / rx;

    // transform local center back to world space:
    // cx = cx'*cos(φ) - cy'*sin(φ) + (x1+x2)/2
    // cy = cx'*sin(φ) + cy'*cos(φ) + (y1+y2)/2
    float cx = cphi * cxp - sphi * cyp + (cur[0] + end[0]) * .5f;
    float cy = sphi * cxp + cphi * cyp + (cur[1] + end[1]) * .5f;

    auto angle = [](float ux, float uy, float vx, float vy)
    {
        float n = sqrtf((ux * ux + uy * uy) * (vx * vx + vy * vy));
        if (n < 1e-8f)
            return 0.f;
        float a = acosf(std::max(-1.f, std::min(1.f, (ux * vx + uy * vy) / n)));
        if (ux * vy - uy * vx < 0)
            a = -a;
        return a;
    };

    // start angle in normalized circle space: θ₁ = angle( (1,0), ((x1'-cx')/rx,
    // (y1'-cy')/ry) )
    float theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);

    // angular sweep: Δθ = angle( ((x1'-cx')/rx, (y1'-cy')/ry), ((-x1'-cx')/rx,
    // (-y1'-cy')/ry) )
    float dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx,
                         (-y1p - cyp) / ry);

    if (!sweep && dtheta > 0)
        dtheta -= 2 * PI;
    if (sweep && dtheta < 0)
        dtheta += 2 * PI;

    int segs = std::max(2, (int)(fabsf(dtheta) / PI * ARC_SEGS + 0.5f));
    for (int i = 1; i <= segs; i++)
    {
        // parameric angle: t = θ₁ + (Δθ * i / segments)
        float t = theta1 + dtheta * i / segs;

        // rotated ellipse parametric equations:
        // x = cos(φ)*rx*cos(t) - sin(φ)*ry*sin(t) + cx
        // y = sin(φ)*rx*cos(t) + cos(φ)*ry*sin(t) + cy
        float x = cphi * rx * cosf(t) - sphi * ry * sinf(t) + cx;
        float y = sphi * rx * cosf(t) + cphi * ry * sinf(t) + cy;
        pts.push_back({x, y});
    }
}

std::vector<Contour> pathToContours(const std::vector<PathSegment> &segs)
{
    std::vector<Contour> contours;
    Contour cur;
    V2 pen = {0, 0}, start = {0, 0};
    V2 lastCP = {0, 0};
    PathCmd lastCmd = PathCmd::MoveTo;

    auto resolve = [&](float x, float y, bool rel) -> V2
    { return rel ? V2{pen[0] + x, pen[1] + y} : V2{x, y}; };

    for (auto &s : segs)
    {
        switch (s.cmd)
        {
        case PathCmd::MoveTo:
        {
            if (!cur.pts.empty())
            {
                contours.push_back(cur);
                cur = {};
            }
            pen = start = resolve(s.args[0], s.args[1], s.relative);
            cur.pts.push_back(pen);
            break;
        }
        case PathCmd::LineTo:
            pen = resolve(s.args[0], s.args[1], s.relative);
            cur.pts.push_back(pen);
            break;
        case PathCmd::HLineTo:
            pen[0] = s.relative ? pen[0] + s.args[0] : s.args[0];
            cur.pts.push_back(pen);
            break;
        case PathCmd::VLineTo:
            pen[1] = s.relative ? pen[1] + s.args[0] : s.args[0];
            cur.pts.push_back(pen);
            break;
        case PathCmd::CubicTo:
        {
            V2 cp1, cp2, ep;
            if (s.args[6] == 1.f)
            { // smooth
                cp1 = (lastCmd == PathCmd::CubicTo)
                          ? V2{2 * pen[0] - lastCP[0], 2 * pen[1] - lastCP[1]}
                          : pen;
            }
            else
            {
                cp1 = resolve(s.args[0], s.args[1], s.relative);
            }
            cp2 = resolve(s.args[2], s.args[3], s.relative);
            ep = resolve(s.args[4], s.args[5], s.relative);
            flattenCubic(cur.pts, pen, cp1, cp2, ep);
            lastCP = cp2;
            pen = ep;
            break;
        }
        case PathCmd::QuadTo:
        {
            V2 cp, ep;
            if (s.args[4] == 1.f)
            { // smooth
                cp = (lastCmd == PathCmd::QuadTo)
                         ? V2{2 * pen[0] - lastCP[0], 2 * pen[1] - lastCP[1]}
                         : pen;
            }
            else
            {
                cp = resolve(s.args[0], s.args[1], s.relative);
            }
            ep = resolve(s.args[2], s.args[3], s.relative);
            flattenQuad(cur.pts, pen, cp, ep);
            lastCP = cp;
            pen = ep;
            break;
        }
        case PathCmd::ArcTo:
        {
            V2 ep = resolve(s.args[5], s.args[6], s.relative);
            flattenArc(cur.pts, pen, s.args[0], s.args[1], s.args[2],
                       s.args[3] > 0.5f, s.args[4] > 0.5f, ep);
            pen = ep;
            break;
        }
        case PathCmd::ClosePath:
            if (!cur.pts.empty())
            {
                cur.closed = true;
                cur.pts.push_back(start);
                contours.push_back(cur);
                cur = {};
            }
            pen = start;
            break;
        }
        lastCmd = s.cmd;
    }
    if (!cur.pts.empty())
        contours.push_back(cur);
    return contours;
}
