#include "earclip.h"
#include <algorithm>
#include <numeric>

namespace earclip
{

static float signedArea(const std::vector<V2> &pts)
{
    float a = 0;
    int n = (int)pts.size();
    for (int i = 0, j = n - 1; i < n; j = i++)
        a += pts[j][0] * pts[i][1] - pts[i][0] * pts[j][1];
    return a * 0.5f;
}

static float cross2(V2 o, V2 a, V2 b)
{
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

std::vector<uint32_t> triangulate(const std::vector<V2> &polyIn)
{
    std::vector<uint32_t> out;
    int n = (int)polyIn.size();
    if (n < 3)
        return out;

    // if area is positive, use forward order; else reverse
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    if (signedArea(polyIn) < 0.f)
        std::reverse(idx.begin(), idx.end());

    int maxIter = n * n;
    while ((int)idx.size() > 3 && maxIter-- > 0)
    {
        int m = (int)idx.size();
        bool found = false;
        for (int i = 0; i < m; i++)
        {
            int prev = (i + m - 1) % m, next = (i + 1) % m;
            V2 a = polyIn[idx[prev]];
            V2 b = polyIn[idx[i]];
            V2 c = polyIn[idx[next]];

            // in CW a convex vertex has cross2(a,b,c) > 0
            if (cross2(a, b, c) <= 0.f)
                continue;

            // no vertex should be inside this ear
            bool ear = true;
            for (int j = 0; j < m && ear; j++)
            {
                if (j == prev || j == i || j == next)
                    continue;
                V2 p = polyIn[idx[j]];

                if (cross2(a, b, p) > 0.f && cross2(b, c, p) > 0.f &&
                    cross2(c, a, p) > 0.f)
                    ear = false;
            }
            if (ear)
            {
                out.push_back((uint32_t)idx[prev]);
                out.push_back((uint32_t)idx[i]);
                out.push_back((uint32_t)idx[next]);
                idx.erase(idx.begin() + i);
                found = true;
                break;
            }
        }
        if (!found)
            break;
    }
    if ((int)idx.size() == 3)
    {
        out.push_back((uint32_t)idx[0]);
        out.push_back((uint32_t)idx[1]);
        out.push_back((uint32_t)idx[2]);
    }
    return out;
}

} // namespace earclip
