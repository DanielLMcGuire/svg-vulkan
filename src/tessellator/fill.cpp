#include "fill.h"
#include "earclip.h"
#include "tessellator_internal.h"
#include <algorithm>
#include <cmath>

void computeBounds(const std::vector<V2> &pts, float &minX, float &minY,
                   float &maxX, float &maxY)
{
    minX = minY = 1e30f;
    maxX = maxY = -1e30f;
    for (auto &p : pts)
    {
        minX = std::min(minX, p[0]);
        minY = std::min(minY, p[1]);
        maxX = std::max(maxX, p[0]);
        maxY = std::max(maxY, p[1]);
    }
}

void appendFillRadial(Mesh &mesh, const std::vector<V2> &ring, V2 center,
                      const PaintEval &paint, int numRings)
{
    int n = (int)ring.size();
    if (n < 3)
        return;

    uint32_t base = (uint32_t)mesh.vertices.size();
    auto pushVert = [&](V2 p)
    {
        Color c = paint(p[0], p[1]);
        float a = c.a;
        mesh.vertices.push_back({p[0], p[1], c.r * a, c.g * a, c.b * a, a});
    };

    pushVert(center); // base + 0
    for (int k = 1; k <= numRings; k++)
    {
        float t = (float)k / (float)numRings;
        for (int i = 0; i < n; i++)
            pushVert({center[0] + (ring[i][0] - center[0]) * t,
                      center[1] + (ring[i][1] - center[1]) * t});
    }
    auto ringStart = [&](int k)
    { return base + 1 + (uint32_t)(k - 1) * (uint32_t)n; };

    for (int i = 0; i < n; i++)
    {
        int j = (i + 1) % n;
        mesh.indices.push_back(base);
        mesh.indices.push_back(ringStart(1) + i);
        mesh.indices.push_back(ringStart(1) + j);
    }
    for (int k = 1; k < numRings; k++)
    {
        for (int i = 0; i < n; i++)
        {
            int j = (i + 1) % n;
            uint32_t a0 = ringStart(k) + i, a1 = ringStart(k) + j;
            uint32_t b0 = ringStart(k + 1) + i, b1 = ringStart(k + 1) + j;
            mesh.indices.push_back(a0);
            mesh.indices.push_back(b0);
            mesh.indices.push_back(b1);
            mesh.indices.push_back(a0);
            mesh.indices.push_back(b1);
            mesh.indices.push_back(a1);
        }
    }
}

void appendFill(Mesh &mesh, const std::vector<V2> &poly, const PaintEval &paint)
{
    if (poly.size() < 3)
        return;
    auto tris = earclip::triangulate(poly);
    if (tris.empty())
    {
        TESSLOG("earclip produced 0 triangles for poly with %d pts",
                (int)poly.size());
        return;
    }

#ifdef _DEBUG
    float minX = poly[0][0], maxX = poly[0][0], minY = poly[0][1],
          maxY = poly[0][1];
    for (auto &p : poly)
    {
        minX = std::min(minX, p[0]);
        maxX = std::max(maxX, p[0]);
        minY = std::min(minY, p[1]);
        maxY = std::max(maxY, p[1]);
    }
    TESSLOG("fill: %d pts, %d tris, bbox=(%.1f,%.1f)-(%.1f,%.1f)",
            (int)poly.size(), (int)tris.size() / 3, minX, minY, maxX, maxY);
#endif

    uint32_t base = (uint32_t)mesh.vertices.size();
    for (auto &p : poly)
    {
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]))
        {
            TESSLOG("WARNING: NaN/Inf vertex (%.3f, %.3f) — skipping fill",
                    p[0], p[1]);
            return;
        }
        Color col = paint(p[0], p[1]);
        float a = col.a;
        mesh.vertices.push_back(
            {p[0], p[1], col.r * a, col.g * a, col.b * a, a});
    }
    for (auto idx : tris)
        mesh.indices.push_back(base + idx);
}

void appendTriangleStrip(Mesh &mesh, const std::vector<V2> &tris,
                         const PaintEval &paint)
{
    if (tris.size() < 3)
        return;

#ifdef _DEBUG
    float minX = tris[0][0], maxX = tris[0][0], minY = tris[0][1],
          maxY = tris[0][1];
    for (auto &p : tris)
    {
        minX = std::min(minX, p[0]);
        maxX = std::max(maxX, p[0]);
        minY = std::min(minY, p[1]);
        maxY = std::max(maxY, p[1]);
    }
    TESSLOG("stroke: %d tris, bbox=(%.1f,%.1f)-(%.1f,%.1f)",
            (int)tris.size() / 3, minX, minY, maxX, maxY);
#endif

    uint32_t base = (uint32_t)mesh.vertices.size();
    for (size_t i = 0; i < tris.size(); i += 3)
    {
        if (i + 2 >= tris.size())
            break;
        for (int k = 0; k < 3; k++)
        {
            Color col = paint(tris[i + k][0], tris[i + k][1]);
            float a = col.a;
            mesh.vertices.push_back({tris[i + k][0], tris[i + k][1], col.r * a,
                                     col.g * a, col.b * a, a});
            mesh.indices.push_back(base + (uint32_t)(i + k));
        }
    }
}

void buildFan(Mesh::StencilFill &sf, const std::vector<V2> &pts)
{
    if (pts.size() < 3)
        return;
    uint32_t base = (uint32_t)sf.verts.size();
    for (auto &p : pts)
        sf.verts.push_back({p[0], p[1], 0.f, 0.f, 0.f, 0.f});
    for (uint32_t i = 1; i + 1 < (uint32_t)pts.size(); i++)
    {
        sf.indices.push_back(base);
        sf.indices.push_back(base + i);
        sf.indices.push_back(base + i + 1);
    }
}
