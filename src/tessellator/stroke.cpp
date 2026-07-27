#include "stroke.h"
#include "fill.h"
#include <algorithm>
#include <cmath>

static V2 normalize(V2 v)
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1]);
    if (len < 1e-8f)
        return {0, 0};
    return {v[0] / len, v[1] / len};
}
static V2 perp(V2 v) { return {-v[1], v[0]}; }

static void appendArcFan(std::vector<V2> &out, V2 center, V2 from, V2 to,
                         float hw, const V2 *preferDir = nullptr)
{
    float a0 = atan2f(from[1], from[0]);
    float a1 = atan2f(to[1], to[0]);
    float da = a1 - a0;
    while (da > PI)
        da -= 2.f * PI;
    while (da < -PI)
        da += 2.f * PI;
    if (preferDir)
    {
        float mid = a0 + da * 0.5f;
        V2 midDir = {cosf(mid), sinf(mid)};
        if (midDir[0] * (*preferDir)[0] + midDir[1] * (*preferDir)[1] < 0.f)
            da = (da > 0.f) ? da - 2.f * PI : da + 2.f * PI;
    }
    int segs = std::max(1, (int)(fabsf(da) / 0.3927f + 0.5f)); // ~22.5 deg/step
    V2 prev = {center[0] + from[0], center[1] + from[1]};
    for (int s = 1; s <= segs; s++)
    {
        float t = a0 + da * s / segs;
        V2 cur = {center[0] + hw * cosf(t), center[1] + hw * sinf(t)};
        out.push_back(center);
        out.push_back(prev);
        out.push_back(cur);
        prev = cur;
    }
}

static void appendJoinWedge(std::vector<V2> &out, V2 vertex, V2 n0, V2 n1,
                            float hw, float sign, LineJoin join,
                            float miterLimit)
{
    V2 o0 = {n0[0] * hw * sign, n0[1] * hw * sign};
    V2 o1 = {n1[0] * hw * sign, n1[1] * hw * sign};

    if (join == LineJoin::Round)
    {
        appendArcFan(out, vertex, o0, o1, hw);
        return;
    }
    if (join == LineJoin::Miter)
    {
        float denom = 1.f + (n0[0] * n1[0] + n0[1] * n1[1]);
        if (denom > 1e-4f)
        {
            float scale = hw / denom;
            V2 offset = {(n0[0] + n1[0]) * scale * sign,
                         (n0[1] + n1[1]) * scale * sign};
            float offLen = sqrtf(offset[0] * offset[0] + offset[1] * offset[1]);
            if (offLen <= hw * miterLimit + 1e-3f)
            {
                V2 tip = {vertex[0] + offset[0], vertex[1] + offset[1]};
                out.push_back(vertex);
                out.push_back({vertex[0] + o0[0], vertex[1] + o0[1]});
                out.push_back(tip);
                out.push_back(vertex);
                out.push_back(tip);
                out.push_back({vertex[0] + o1[0], vertex[1] + o1[1]});
                return;
            }
            // exceeds miterlimit, fall back to bevel
        }
    }
    // bevel (and fallback)
    out.push_back(vertex);
    out.push_back({vertex[0] + o0[0], vertex[1] + o0[1]});
    out.push_back({vertex[0] + o1[0], vertex[1] + o1[1]});
}

static void appendCap(std::vector<V2> &out, V2 endpoint, V2 normal, V2 outward,
                      float hw, LineCap cap)
{
    if (cap == LineCap::Butt)
        return;
    V2 l = {normal[0] * hw, normal[1] * hw};
    V2 r = {-normal[0] * hw, -normal[1] * hw};
    if (cap == LineCap::Square)
    {
        V2 ext = {outward[0] * hw, outward[1] * hw};
        V2 la = {endpoint[0] + l[0], endpoint[1] + l[1]};
        V2 ra = {endpoint[0] + r[0], endpoint[1] + r[1]};
        V2 lb = {la[0] + ext[0], la[1] + ext[1]};
        V2 rb = {ra[0] + ext[0], ra[1] + ext[1]};
        out.insert(out.end(), {la, ra, rb, la, rb, lb});
    }
    else
    { // round
        appendArcFan(out, endpoint, l, r, hw, &outward);
    }
}

static void appendStrokeContour(std::vector<V2> &fillPoly,
                                const std::vector<V2> &ptsIn, float hw,
                                bool closed, LineCap cap, LineJoin join,
                                float miterLimit)
{
    std::vector<V2> pts = ptsIn;
    if (closed && pts.size() > 1)
    {
        V2 f = pts.front(), b = pts.back();
        if (fabsf(f[0] - b[0]) < 1e-6f && fabsf(f[1] - b[1]) < 1e-6f)
            pts.pop_back();
    }
    int n = (int)pts.size();
    if (n < 2)
        return;

    int segCount = closed ? n : n - 1;
    if (segCount < 1)
        return;

    std::vector<V2> normals(segCount);
    for (int i = 0; i < segCount; i++)
    {
        V2 a = pts[i], b = pts[(i + 1) % n];
        normals[i] = perp(normalize({b[0] - a[0], b[1] - a[1]}));
    }

    for (int i = 0; i < segCount; i++)
    {
        V2 a = pts[i], b = pts[(i + 1) % n];
        V2 no = normals[i];
        V2 la = {a[0] + no[0] * hw, a[1] + no[1] * hw},
           ra = {a[0] - no[0] * hw, a[1] - no[1] * hw};
        V2 lb = {b[0] + no[0] * hw, b[1] + no[1] * hw},
           rb = {b[0] - no[0] * hw, b[1] - no[1] * hw};
        fillPoly.insert(fillPoly.end(), {la, ra, rb, la, rb, lb});
    }

    int joinCount = closed ? segCount : segCount - 1;
    for (int k = 0; k < joinCount; k++)
    {
        int i = (k + 1) % n;
        V2 n0 = normals[k];
        V2 n1 = normals[(k + 1) % segCount];

        float dot = n0[0] * n1[0] + n0[1] * n1[1];
        float cross = n0[0] * n1[1] - n0[1] * n1[0];
        if (fabsf(cross) < 1e-5f && dot > 0.f)
            continue;

        float sign = (cross < 0.f) ? 1.f : -1.f;
        appendJoinWedge(fillPoly, pts[i], n0, n1, hw, sign, join, miterLimit);
        appendJoinWedge(fillPoly, pts[i], n0, n1, hw, -sign, LineJoin::Bevel,
                        miterLimit);
    }

    if (!closed)
    {
        V2 dir0 = normalize({pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]});
        V2 outward0 = {-dir0[0], -dir0[1]};
        appendCap(fillPoly, pts[0], normals[0], outward0, hw, cap);

        V2 dirN = normalize(
            {pts[n - 1][0] - pts[n - 2][0], pts[n - 1][1] - pts[n - 2][1]});
        appendCap(fillPoly, pts[n - 1], normals[segCount - 1], dirN, hw, cap);
    }
}

std::vector<V2> makeRoundedRect(float x, float y, float w, float h, float rx,
                                float ry, int arcSegs)
{
    std::vector<V2> pts;
    rx = std::min(rx, w * .5f);
    ry = std::min(ry, h * .5f);
    auto corner = [&](float cx, float cy, float startA, float endA)
    {
        for (int i = 0; i <= arcSegs; i++)
        {
            float t = startA + (endA - startA) * i / arcSegs;
            pts.push_back({cx + rx * cosf(t), cy + ry * sinf(t)});
        }
    };
    corner(x + rx, y + ry, PI, PI * 1.5f);
    corner(x + w - rx, y + ry, PI * 1.5f, 2 * PI);
    corner(x + w - rx, y + h - ry, 0, PI * .5f);
    corner(x + rx, y + h - ry, PI * .5f, PI);
    return pts;
}

std::vector<V2> makeEllipse(float cx, float cy, float rx, float ry, int segs)
{
    std::vector<V2> pts;
    for (int i = 0; i < segs; i++)
    {
        float t = 2 * PI * i / segs;
        pts.push_back({cx + rx * cosf(t), cy + ry * sinf(t)});
    }
    return pts;
}

static std::vector<std::vector<V2>>
applyDash(const std::vector<V2> &pts, bool closed,
          const std::vector<float> &dashArray, float dashOffset)
{
    if (dashArray.empty() || pts.size() < 2)
        return {pts};

    // close loop
    std::vector<V2> work = pts;
    if (closed)
    {
        const V2 &f = work.front(), &b = work.back();
        if (f[0] != b[0] || f[1] != b[1])
            work.push_back(f);
    }
    int n = (int)work.size();

    float patLen = 0.f;
    for (float d : dashArray)
        patLen += d;
    if (patLen < 1e-6f)
        return {};

    // normalise dashOffset into [0, patLen)
    float off = fmodf(dashOffset, patLen);
    if (off < 0.f)
        off += patLen;

    // find the starting phase (index into dashArray) and how much remains
    int phaseIdx = 0;
    float phaseRemain = dashArray[0];
    if (off > 0.f)
    {
        float rem = off;
        while (rem > 0.f)
        {
            if (rem < dashArray[phaseIdx])
            {
                phaseRemain = dashArray[phaseIdx] - rem;
                break;
            }
            rem -= dashArray[phaseIdx];
            phaseIdx = (phaseIdx + 1) % (int)dashArray.size();
            phaseRemain = dashArray[phaseIdx];
        }
    }
    bool drawing = (phaseIdx % 2 == 0);

    std::vector<std::vector<V2>> result;
    std::vector<V2> cur;
    if (drawing)
        cur.push_back(work[0]);

    for (int i = 0; i < n - 1; i++)
    {
        float dx = work[i + 1][0] - work[i][0];
        float dy = work[i + 1][1] - work[i][1];
        float segLen = sqrtf(dx * dx + dy * dy);
        if (segLen < 1e-8f)
            continue;

        float consumed = 0.f;
        bool lastWasBdy = false;

        while (consumed < segLen - 1e-6f)
        {
            float step = std::min(phaseRemain, segLen - consumed);
            consumed += step;
            phaseRemain -= step;

            lastWasBdy = (phaseRemain < 1e-6f);
            if (lastWasBdy)
            {
                float t = consumed / segLen;
                V2 p = {work[i][0] + t * dx, work[i][1] + t * dy};
                if (drawing)
                {
                    cur.push_back(p);
                    if (cur.size() >= 2)
                        result.push_back(std::move(cur));
                    cur.clear();
                }
                phaseIdx = (phaseIdx + 1) % (int)dashArray.size();
                phaseRemain = dashArray[phaseIdx];
                drawing = (phaseIdx % 2 == 0);
                if (drawing)
                    cur.push_back(p);
            }
        }

        // add segment endpoint
        if (drawing && !lastWasBdy)
            cur.push_back(work[i + 1]);
    }

    if (drawing && cur.size() >= 2)
        result.push_back(std::move(cur));
    return result;
}

void strokeWithDash(Mesh &out, const std::vector<V2> &pts, bool closed,
                    const Style &st, const PaintEval &paint)
{
    float hw = st.strokeWidth * 0.5f;
    auto segments = applyDash(pts, closed, st.dashArray, st.dashOffset);
    bool segClosed = closed && st.dashArray.empty();
    for (auto &seg : segments)
    {
        if (seg.size() < 2)
            continue;
        std::vector<V2> stris;
        appendStrokeContour(stris, seg, hw, segClosed, st.lineCap, st.lineJoin,
                            st.miterLimit);
        appendTriangleStrip(out, stris, paint);
    }
}
