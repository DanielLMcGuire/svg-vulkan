#include "tessellator.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <chrono>
#include <functional>
#include <unordered_map>

static const float PI = 3.14159265358979f;

#ifdef _DEBUG
    #ifdef _WIN32
        #ifndef WIN32_LEAN_AND_MEAN
            #define WIN32_LEAN_AND_MEAN
        #endif
        #ifndef NOMINMAX
            #define NOMINMAX
        #endif
        #include <windows.h>
        #define TESSLOG(fmt, ...) do { \
            char _buf[512]; \
            snprintf(_buf, sizeof(_buf), "[TESS] " fmt "\n", ##__VA_ARGS__); \
            OutputDebugStringA(_buf); \
            printf("%s", _buf); \
        } while(0)
    #else
        #include <cstdio>
        #define TESSLOG(fmt, ...) do { \
            fprintf(stderr, "[TESS] " fmt "\n", ##__VA_ARGS__); \
        } while(0)
    #endif
#else
    #define TESSLOG(fmt, ...) do {} while(0)
#endif

using V2 = std::array<float,2>;

namespace earclip {

static float signedArea(const std::vector<V2>& pts) {
    float a = 0;
    int n = (int)pts.size();
    for(int i=0,j=n-1;i<n;j=i++) a+=pts[j][0]*pts[i][1]-pts[i][0]*pts[j][1];
    return a*0.5f;
}

static float cross2(V2 o, V2 a, V2 b) {
    return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0]);
}

static std::vector<uint32_t> triangulate(const std::vector<V2>& polyIn) {
    std::vector<uint32_t> out;
    int n = (int)polyIn.size();
    if(n < 3) return out;

    // if area is positive, use forward order; else reverse
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    if(signedArea(polyIn) < 0.f)
        std::reverse(idx.begin(), idx.end());

    int maxIter = n * n;   // O(n²) worst case
    while((int)idx.size() > 3 && maxIter-- > 0) {
        int m = (int)idx.size();
        bool found = false;
        for(int i = 0; i < m; i++) {
            int prev = (i + m - 1) % m, next = (i + 1) % m;
            V2 a = polyIn[idx[prev]];
            V2 b = polyIn[idx[i]];
            V2 c = polyIn[idx[next]];

            // in CW a convex vertex has cross2(a,b,c) > 0
            if(cross2(a, b, c) <= 0.f) continue;

            // no vertex should be inside this ear
            bool ear = true;
            for(int j = 0; j < m && ear; j++) {
                if(j == prev || j == i || j == next) continue;
                V2 p = polyIn[idx[j]];

                if(cross2(a,b,p) > 0.f &&
                   cross2(b,c,p) > 0.f &&
                   cross2(c,a,p) > 0.f)
                    ear = false;
            }
            if(ear) {
                out.push_back((uint32_t)idx[prev]);
                out.push_back((uint32_t)idx[i]);
                out.push_back((uint32_t)idx[next]);
                idx.erase(idx.begin() + i);
                found = true;
                break;
            }
        }
        if(!found) break;
    }
    if((int)idx.size() == 3) {
        out.push_back((uint32_t)idx[0]);
        out.push_back((uint32_t)idx[1]);
        out.push_back((uint32_t)idx[2]);
    }
    return out;
}

} // namespace earclip

static void flattenCubic(std::vector<V2>& pts,
    V2 p0, V2 p1, V2 p2, V2 p3, int depth=0)
{
    if(depth>8) { pts.push_back(p3); return; }

    // calc deviation vectors from the baseline chord
    float ux = 3.0f * p1[0] - 2.0f * p0[0] - p3[0];
    float uy = 3.0f * p1[1] - 2.0f * p0[1] - p3[1];
    float vx = 3.0f * p2[0] - p0[0] - 2.0f * p3[0];
    float vy = 3.0f * p2[1] - p0[1] - 2.0f * p3[1];

    // max squared deviation
    float max_dev_sq = std::max(ux * ux + uy * uy, vx * vx + vy * vy);

    // flatness check: max error is 1/4 of dev vector. 
    // squared check: dev^2 <= 16 * TOL^2
    if(max_dev_sq <= 16.0f * CURVE_TOL * CURVE_TOL){ 
        pts.push_back(p3); 
        return; 
    }

    // De Casteljau algo for splitting of a cubic Bézier curve at t=0.5:
    auto mid=[](V2 a,V2 b){ return V2{(a[0]+b[0])*.5f,(a[1]+b[1])*.5f}; };
    
    V2 m01=mid(p0,p1),m12=mid(p1,p2),m23=mid(p2,p3);
    V2 m012=mid(m01,m12),m123=mid(m12,m23);
    V2 m0123=mid(m012,m123);

    flattenCubic(pts,p0,m01,m012,m0123,depth+1);
    flattenCubic(pts,m0123,m123,m23,p3,depth+1);
}

static void flattenQuad(std::vector<V2>& pts,
    V2 p0, V2 p1, V2 p2, int depth=0)
{
    if(depth>8) { pts.push_back(p2); return; }

    // quadratic deviation vector
    float ux = 2.0f * p1[0] - p0[0] - p2[0];
    float uy = 2.0f * p1[1] - p0[1] - p2[1];

    // flatness check: dev^2 <= 16 * TOL^2
    if((ux * ux + uy * uy) <= 16.0f * CURVE_TOL * CURVE_TOL){ 
        pts.push_back(p2); 
        return; 
    }

    // De Casteljau
    auto mid=[](V2 a,V2 b){ return V2{(a[0]+b[0])*.5f,(a[1]+b[1])*.5f}; };
    
    V2 m01=mid(p0,p1),m12=mid(p1,p2);
    V2 m0112=mid(m01,m12);

    flattenQuad(pts,p0,m01,m0112,depth+1);
    flattenQuad(pts,m0112,m12,p2,depth+1);
}

static void flattenArc(std::vector<V2>& pts,
    V2 cur, float rx, float ry, float xRot,
    bool largeArc, bool sweep, V2 end)
{
    if(rx==0||ry==0){ pts.push_back(end); return; }

    // convert rotation to radians: φ = xRot * (π / 180)
    float phi = xRot*PI/180.f;
    float cphi=cosf(phi), sphi=sinf(phi);

    // relative half-distance from end to start: Δx = (x1 - x2)/2, Δy = (y1 - y2)/2
    float dx2=(cur[0]-end[0])*.5f, dy2=(cur[1]-end[1])*.5f;

    // rotate half-distance into ellipse local space: 
    // x1' = Δx*cos(φ) + Δy*sin(φ)
    // y1' = -Δx*sin(φ) + Δy*cos(φ)
    float x1p= cphi*dx2+sphi*dy2, y1p=-sphi*dx2+cphi*dy2;

    rx=fabsf(rx); ry=fabsf(ry);

    // check if endpoints are within ellipse bounds: λ = (x1'/rx)² + (y1'/ry)²
    float lam=x1p*x1p/(rx*rx)+y1p*y1p/(ry*ry);
    if(lam>1){ 
        // scale radii if endpoints are too far: rx = rx * √λ, ry = ry * √λ
        float s=sqrtf(lam); rx*=s; ry*=s; 
    }

    // calc center offset components:
    // num = (rx²*ry²) - (rx²*y1'²) - (ry²*x1'²)
    // den = (rx²*y1'²) + (ry²*x1'²)
    float num=(rx*rx*ry*ry - rx*rx*y1p*y1p - ry*ry*x1p*x1p);
    float den=(rx*rx*y1p*y1p + ry*ry*x1p*x1p);
    
    // solve for center scaling factor: sq = √(num / den)
    float sq = (den>0)?sqrtf(fabsf(num/den)):0;
    if(largeArc==sweep) sq=-sq;

    // local center coordinates: cx' = (sq * rx * y1') / ry, cy' = -(sq * ry * x1') / rx
    float cxp= sq*rx*y1p/ry, cyp=-sq*ry*x1p/rx;

    // transform local center back to world space:
    // cx = cx'*cos(φ) - cy'*sin(φ) + (x1+x2)/2
    // cy = cx'*sin(φ) + cy'*cos(φ) + (y1+y2)/2
    float cx=cphi*cxp-sphi*cyp+(cur[0]+end[0])*.5f;
    float cy=sphi*cxp+cphi*cyp+(cur[1]+end[1])*.5f;

    auto angle=[](float ux,float uy,float vx,float vy){
        float n=sqrtf((ux*ux+uy*uy)*(vx*vx+vy*vy));
        if(n<1e-8f) return 0.f;
        float a=acosf(std::max(-1.f,std::min(1.f,(ux*vx+uy*vy)/n)));
        if(ux*vy-uy*vx<0) a=-a;
        return a;
    };

    // start angle in normalized circle space: θ₁ = angle( (1,0), ((x1'-cx')/rx, (y1'-cy')/ry) )
    float theta1=angle(1,0,(x1p-cxp)/rx,(y1p-cyp)/ry);
    
    // angular sweep: Δθ = angle( ((x1'-cx')/rx, (y1'-cy')/ry), ((-x1'-cx')/rx, (-y1'-cy')/ry) )
    float dtheta=angle((x1p-cxp)/rx,(y1p-cyp)/ry,(-x1p-cxp)/rx,(-y1p-cyp)/ry);
    
    if(!sweep && dtheta>0) dtheta-=2*PI;
    if( sweep && dtheta<0) dtheta+=2*PI;

    int segs=std::max(2,(int)(fabsf(dtheta)/PI*ARC_SEGS+0.5f));
    for(int i=1;i<=segs;i++){
        // parameric angle: t = θ₁ + (Δθ * i / segments)
        float t=theta1+dtheta*i/segs;
        
        // rotated ellipse parametric equations:
        // x = cos(φ)*rx*cos(t) - sin(φ)*ry*sin(t) + cx
        // y = sin(φ)*rx*cos(t) + cos(φ)*ry*sin(t) + cy
        float x=cphi*rx*cosf(t)-sphi*ry*sinf(t)+cx;
        float y=sphi*rx*cosf(t)+cphi*ry*sinf(t)+cy;
        pts.push_back({x,y});
    }
}

struct Contour { std::vector<V2> pts; bool closed=false; };

static std::vector<Contour> pathToContours(const std::vector<PathSegment>& segs) {
    std::vector<Contour> contours;
    Contour cur;
    V2 pen={0,0}, start={0,0};
    V2 lastCP={0,0};
    PathCmd lastCmd = PathCmd::MoveTo;

    auto resolve = [&](float x, float y, bool rel) -> V2 {
        return rel ? V2{pen[0]+x, pen[1]+y} : V2{x,y};
    };

    for(auto& s : segs) {
        switch(s.cmd) {
        case PathCmd::MoveTo: {
            if(!cur.pts.empty()) { contours.push_back(cur); cur={}; }
            pen = start = resolve(s.args[0],s.args[1],s.relative);
            cur.pts.push_back(pen);
            break;
        }
        case PathCmd::LineTo:
            pen = resolve(s.args[0],s.args[1],s.relative);
            cur.pts.push_back(pen);
            break;
        case PathCmd::HLineTo:
            pen[0] = s.relative ? pen[0]+s.args[0] : s.args[0];
            cur.pts.push_back(pen);
            break;
        case PathCmd::VLineTo:
            pen[1] = s.relative ? pen[1]+s.args[0] : s.args[0];
            cur.pts.push_back(pen);
            break;
        case PathCmd::CubicTo: {
            V2 cp1, cp2, ep;
            if(s.args[6]==1.f) {  // smooth
                cp1 = (lastCmd==PathCmd::CubicTo)
                    ? V2{2*pen[0]-lastCP[0], 2*pen[1]-lastCP[1]} : pen;
            } else {
                cp1 = resolve(s.args[0],s.args[1],s.relative);
            }
            cp2 = resolve(s.args[2],s.args[3],s.relative);
            ep  = resolve(s.args[4],s.args[5],s.relative);
            flattenCubic(cur.pts, pen, cp1, cp2, ep);
            lastCP = cp2;
            pen = ep;
            break;
        }
        case PathCmd::QuadTo: {
            V2 cp, ep;
            if(s.args[4]==1.f) {  // smooth
                cp = (lastCmd==PathCmd::QuadTo)
                    ? V2{2*pen[0]-lastCP[0], 2*pen[1]-lastCP[1]} : pen;
            } else {
                cp = resolve(s.args[0],s.args[1],s.relative);
            }
            ep = resolve(s.args[2],s.args[3],s.relative);
            flattenQuad(cur.pts, pen, cp, ep);
            lastCP = cp;
            pen = ep;
            break;
        }
        case PathCmd::ArcTo: {
            V2 ep = resolve(s.args[5],s.args[6],s.relative);
            flattenArc(cur.pts, pen,
                s.args[0],s.args[1],s.args[2],
                s.args[3]>0.5f, s.args[4]>0.5f, ep);
            pen = ep;
            break;
        }
        case PathCmd::ClosePath:
            if(!cur.pts.empty()) {
                cur.closed = true;
                cur.pts.push_back(start);
                contours.push_back(cur); cur={};
            }
            pen = start;
            break;
        }
        lastCmd = s.cmd;
    }
    if(!cur.pts.empty()) contours.push_back(cur);
    return contours;
}

static V2 normalize(V2 v) {
    float len=sqrtf(v[0]*v[0]+v[1]*v[1]);
    if(len<1e-8f) return {0,0};
    return {v[0]/len,v[1]/len};
}
static V2 perp(V2 v){ return {-v[1],v[0]}; }

static void appendArcFan(std::vector<V2>& out, V2 center, V2 from, V2 to,
                          float hw, const V2* preferDir = nullptr)
{
    float a0 = atan2f(from[1], from[0]);
    float a1 = atan2f(to[1],   to[0]);
    float da = a1 - a0;
    while(da >  PI) da -= 2.f*PI;
    while(da < -PI) da += 2.f*PI;
    if(preferDir) {
        float mid = a0 + da*0.5f;
        V2 midDir = {cosf(mid), sinf(mid)};
        if(midDir[0]*(*preferDir)[0] + midDir[1]*(*preferDir)[1] < 0.f)
            da = (da > 0.f) ? da - 2.f*PI : da + 2.f*PI;
    }
    int segs = std::max(1, (int)(fabsf(da)/0.3927f + 0.5f)); // ~22.5 deg/step
    V2 prev = { center[0]+from[0], center[1]+from[1] };
    for(int s = 1; s <= segs; s++) {
        float t = a0 + da*s/segs;
        V2 cur = { center[0]+hw*cosf(t), center[1]+hw*sinf(t) };
        out.push_back(center); out.push_back(prev); out.push_back(cur);
        prev = cur;
    }
}

static void appendJoinWedge(std::vector<V2>& out, V2 vertex, V2 n0, V2 n1,
                             float hw, float sign, LineJoin join, float miterLimit)
{
    V2 o0 = { n0[0]*hw*sign, n0[1]*hw*sign };
    V2 o1 = { n1[0]*hw*sign, n1[1]*hw*sign };

    if(join == LineJoin::Round) {
        appendArcFan(out, vertex, o0, o1, hw);
        return;
    }
    if(join == LineJoin::Miter) {
        float denom = 1.f + (n0[0]*n1[0] + n0[1]*n1[1]);
        if(denom > 1e-4f) {
            float scale = hw/denom;
            V2 offset = { (n0[0]+n1[0])*scale*sign, (n0[1]+n1[1])*scale*sign };
            float offLen = sqrtf(offset[0]*offset[0] + offset[1]*offset[1]);
            if(offLen <= hw*miterLimit + 1e-3f) {
                V2 tip = { vertex[0]+offset[0], vertex[1]+offset[1] };
                out.push_back(vertex); out.push_back({vertex[0]+o0[0],vertex[1]+o0[1]}); out.push_back(tip);
                out.push_back(vertex); out.push_back(tip); out.push_back({vertex[0]+o1[0],vertex[1]+o1[1]});
                return;
            }
            // exceeds miterlimit, fall back to bevel
        }
    }
    // bevel (and fallback)
    out.push_back(vertex);
    out.push_back({vertex[0]+o0[0], vertex[1]+o0[1]});
    out.push_back({vertex[0]+o1[0], vertex[1]+o1[1]});
}

static void appendCap(std::vector<V2>& out, V2 endpoint, V2 normal, V2 outward,
                       float hw, LineCap cap)
{
    if(cap == LineCap::Butt) return;
    V2 l = {  normal[0]*hw,  normal[1]*hw };
    V2 r = { -normal[0]*hw, -normal[1]*hw };
    if(cap == LineCap::Square) {
        V2 ext = { outward[0]*hw, outward[1]*hw };
        V2 la = {endpoint[0]+l[0], endpoint[1]+l[1]};
        V2 ra = {endpoint[0]+r[0], endpoint[1]+r[1]};
        V2 lb = {la[0]+ext[0], la[1]+ext[1]};
        V2 rb = {ra[0]+ext[0], ra[1]+ext[1]};
        out.insert(out.end(), {la, ra, rb, la, rb, lb});
    } else { // round
        appendArcFan(out, endpoint, l, r, hw, &outward);
    }
}

static void appendStrokeContour(std::vector<V2>& fillPoly,
    const std::vector<V2>& ptsIn, float hw, bool closed,
    LineCap cap, LineJoin join, float miterLimit)
{
    std::vector<V2> pts = ptsIn;
    if(closed && pts.size() > 1) {
        V2 f = pts.front(), b = pts.back();
        if(fabsf(f[0]-b[0]) < 1e-6f && fabsf(f[1]-b[1]) < 1e-6f) pts.pop_back();
    }
    int n = (int)pts.size();
    if(n < 2) return;

    int segCount = closed ? n : n - 1;
    if(segCount < 1) return;

    std::vector<V2> normals(segCount);
    for(int i = 0; i < segCount; i++) {
        V2 a = pts[i], b = pts[(i+1)%n];
        normals[i] = perp(normalize({b[0]-a[0], b[1]-a[1]}));
    }

    for(int i = 0; i < segCount; i++) {
        V2 a = pts[i], b = pts[(i+1)%n];
        V2 no = normals[i];
        V2 la = {a[0]+no[0]*hw, a[1]+no[1]*hw}, ra = {a[0]-no[0]*hw, a[1]-no[1]*hw};
        V2 lb = {b[0]+no[0]*hw, b[1]+no[1]*hw}, rb = {b[0]-no[0]*hw, b[1]-no[1]*hw};
        fillPoly.insert(fillPoly.end(), {la, ra, rb,  la, rb, lb});
    }

    int joinCount = closed ? segCount : segCount - 1;
    for(int k = 0; k < joinCount; k++) {
        int i      = (k+1) % n;
        V2  n0     = normals[k];
        V2  n1     = normals[(k+1) % segCount];

        float dot   = n0[0]*n1[0] + n0[1]*n1[1];
        float cross = n0[0]*n1[1] - n0[1]*n1[0];
        if(fabsf(cross) < 1e-5f && dot > 0.f) continue;

        float sign = (cross < 0.f) ? 1.f : -1.f;
        appendJoinWedge(fillPoly, pts[i], n0, n1, hw, sign, join, miterLimit);
        appendJoinWedge(fillPoly, pts[i], n0, n1, hw, -sign, LineJoin::Bevel, miterLimit);
    }

    if(!closed) {
        V2 dir0 = normalize({pts[1][0]-pts[0][0], pts[1][1]-pts[0][1]});
        V2 outward0 = {-dir0[0], -dir0[1]};
        appendCap(fillPoly, pts[0], normals[0], outward0, hw, cap);

        V2 dirN = normalize({pts[n-1][0]-pts[n-2][0], pts[n-1][1]-pts[n-2][1]});
        appendCap(fillPoly, pts[n-1], normals[segCount-1], dirN, hw, cap);
    }
}

static std::vector<V2> makeRoundedRect(
    float x,float y,float w,float h,float rx,float ry, int arcSegs=16)
{
    std::vector<V2> pts;
    rx=std::min(rx,w*.5f); ry=std::min(ry,h*.5f);
    auto corner=[&](float cx,float cy,float startA,float endA){
        for(int i=0;i<=arcSegs;i++){
            float t=startA+(endA-startA)*i/arcSegs;
            pts.push_back({cx+rx*cosf(t), cy+ry*sinf(t)});
        }
    };
    corner(x+rx,    y+ry,    PI,   PI*1.5f);
    corner(x+w-rx,  y+ry,    PI*1.5f, 2*PI);
    corner(x+w-rx,  y+h-ry,  0,    PI*.5f);
    corner(x+rx,    y+h-ry,  PI*.5f, PI);
    return pts;
}

static std::vector<V2> makeEllipse(float cx,float cy,float rx,float ry, int segs=64){
    std::vector<V2> pts;
    for(int i=0;i<segs;i++){
        float t=2*PI*i/segs;
        pts.push_back({cx+rx*cosf(t), cy+ry*sinf(t)});
    }
    return pts;
}

using PaintEval = std::function<Color(float,float)>;

static Color sampleGradientStops(const std::vector<GradientStop>& stops, float t, SpreadMethod spread) {
    if(stops.empty()) return {0,0,0,0};
    switch(spread) {
    case SpreadMethod::Pad:     t = std::max(0.f, std::min(1.f, t)); break;
    case SpreadMethod::Repeat:  t = t - floorf(t); break;
    case SpreadMethod::Reflect: {
        float m = fmodf(fabsf(t), 2.f);
        t = (m > 1.f) ? 2.f - m : m;
        break;
    }
    }
    if(stops.size() == 1) return stops[0].color;
    if(t <= stops.front().offset) return stops.front().color;
    if(t >= stops.back().offset)  return stops.back().color;
    for(size_t i = 0; i + 1 < stops.size(); i++) {
        float o0 = stops[i].offset, o1 = stops[i+1].offset;
        if(t >= o0 && t <= o1) {
            float f = (o1 > o0) ? (t - o0) / (o1 - o0) : 0.f;
            const Color& c0 = stops[i].color;
            const Color& c1 = stops[i+1].color;
            return { c0.r + (c1.r-c0.r)*f, c0.g + (c1.g-c0.g)*f,
                     c0.b + (c1.b-c0.b)*f, c0.a + (c1.a-c0.a)*f };
        }
    }
    return stops.back().color;
}

static PaintEval makePaintEval(const Paint& paint, const Style& style,
    const std::unordered_map<std::string,GradientDef>* gradients,
    float bboxMinX, float bboxMinY, float bboxW, float bboxH,
    float opacityMul)
{
    if(paint.none)
        return [](float,float){ return Color::none(); };

    if(paint.isGradient() && gradients) {
        auto it = gradients->find(paint.gradientId);
        if(it != gradients->end() && !it->second.stops.empty()) {
            const GradientDef* g = &it->second;
            float safeW = (fabsf(bboxW) < 1e-6f) ? 1.f : bboxW;
            float safeH = (fabsf(bboxH) < 1e-6f) ? 1.f : bboxH;
            return [g, bboxMinX, bboxMinY, safeW, safeH, opacityMul](float px, float py) -> Color {
                float nx, ny;
                if(g->userSpaceOnUse) { nx = px; ny = py; }
                else { nx = (px - bboxMinX) / safeW; ny = (py - bboxMinY) / safeH; }
                auto gp = g->gradientTransform.apply(nx, ny);

                float t;
                if(g->kind == GradientKind::Linear) {
                    float dx = g->x2 - g->x1, dy = g->y2 - g->y1;
                    float len2 = dx*dx + dy*dy;
                    t = (len2 < 1e-12f) ? 0.f : ((gp[0]-g->x1)*dx + (gp[1]-g->y1)*dy) / len2;
                } else {
                    float pfx = gp[0]-g->fx, pfy = gp[1]-g->fy;
                    float cfx = g->fx-g->cx, cfy = g->fy-g->cy;
                    float a = pfx*pfx + pfy*pfy;
                    if(a < 1e-12f) t = 0.f;
                    else {
                        float b = 2.f*(cfx*pfx + cfy*pfy);
                        float c = cfx*cfx + cfy*cfy - g->r*g->r;
                        float disc = std::max(0.f, b*b - 4.f*a*c);
                        float ts = (-b + sqrtf(disc)) / (2.f*a);
                        t = (ts > 1e-6f) ? 1.f/ts : 0.f;
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
    return [base](float,float){ return base; };
}

static void computeBounds(const std::vector<V2>& pts, float& minX, float& minY, float& maxX, float& maxY) {
    minX = minY = 1e30f; maxX = maxY = -1e30f;
    for(auto& p : pts) {
        minX = std::min(minX, p[0]); minY = std::min(minY, p[1]);
        maxX = std::max(maxX, p[0]); maxY = std::max(maxY, p[1]);
    }
}

static bool paintIsRadialGradient(const Paint& paint,
    const std::unordered_map<std::string,GradientDef>* gradients)
{
    if(!paint.isGradient() || !gradients) return false;
    auto it = gradients->find(paint.gradientId);
    return it != gradients->end() && it->second.kind == GradientKind::Radial
        && !it->second.stops.empty();
}

static void appendFillRadial(Mesh& mesh, const std::vector<V2>& ring,
    V2 center, const PaintEval& paint, int numRings = 10)
{
    int n = (int)ring.size();
    if(n < 3) return;

    uint32_t base = (uint32_t)mesh.vertices.size();
    auto pushVert = [&](V2 p){
        Color c = paint(p[0], p[1]);
        float a = c.a;
        mesh.vertices.push_back({p[0], p[1], c.r*a, c.g*a, c.b*a, a});
    };

    pushVert(center); // base + 0
    for(int k = 1; k <= numRings; k++) {
        float t = (float)k / (float)numRings;
        for(int i = 0; i < n; i++)
            pushVert({ center[0] + (ring[i][0]-center[0])*t,
                       center[1] + (ring[i][1]-center[1])*t });
    }
    auto ringStart = [&](int k){ return base + 1 + (uint32_t)(k-1)*(uint32_t)n; };

    for(int i = 0; i < n; i++) {
        int j = (i+1)%n;
        mesh.indices.push_back(base);
        mesh.indices.push_back(ringStart(1)+i);
        mesh.indices.push_back(ringStart(1)+j);
    }
    for(int k = 1; k < numRings; k++) {
        for(int i = 0; i < n; i++) {
            int j = (i+1)%n;
            uint32_t a0=ringStart(k)+i,   a1=ringStart(k)+j;
            uint32_t b0=ringStart(k+1)+i, b1=ringStart(k+1)+j;
            mesh.indices.push_back(a0); mesh.indices.push_back(b0); mesh.indices.push_back(b1);
            mesh.indices.push_back(a0); mesh.indices.push_back(b1); mesh.indices.push_back(a1);
        }
    }
}

static void appendFill(Mesh& mesh,
    const std::vector<V2>& poly, const PaintEval& paint)
{
    if(poly.size()<3) return;
    auto tris = earclip::triangulate(poly);
    if(tris.empty()) {
        TESSLOG("earclip produced 0 triangles for poly with %d pts", (int)poly.size());
        return;
    }

#ifdef _DEBUG
    float minX=poly[0][0],maxX=poly[0][0],minY=poly[0][1],maxY=poly[0][1];
    for(auto& p : poly){
        minX=std::min(minX,p[0]); maxX=std::max(maxX,p[0]);
        minY=std::min(minY,p[1]); maxY=std::max(maxY,p[1]);
    }
    TESSLOG("fill: %d pts, %d tris, bbox=(%.1f,%.1f)-(%.1f,%.1f)",
        (int)poly.size(), (int)tris.size()/3,
        minX,minY,maxX,maxY);
#endif

    uint32_t base = (uint32_t)mesh.vertices.size();
    for(auto& p : poly) {
        if(!std::isfinite(p[0]) || !std::isfinite(p[1])) {
            TESSLOG("WARNING: NaN/Inf vertex (%.3f, %.3f) — skipping fill", p[0], p[1]);
            return;
        }
        Color col = paint(p[0], p[1]);
        float a = col.a;
        mesh.vertices.push_back({p[0],p[1], col.r*a,col.g*a,col.b*a,a});
    }
    for(auto idx : tris)
        mesh.indices.push_back(base+idx);
}

static void appendTriangleStrip(Mesh& mesh,
    const std::vector<V2>& tris, const PaintEval& paint)
{
    if(tris.size()<3) return;

#ifdef _DEBUG
    float minX=tris[0][0],maxX=tris[0][0],minY=tris[0][1],maxY=tris[0][1];
    for(auto& p : tris){
        minX=std::min(minX,p[0]); maxX=std::max(maxX,p[0]);
        minY=std::min(minY,p[1]); maxY=std::max(maxY,p[1]);
    }
    TESSLOG("stroke: %d tris, bbox=(%.1f,%.1f)-(%.1f,%.1f)",
        (int)tris.size()/3,
        minX,minY,maxX,maxY);
#endif

    uint32_t base=(uint32_t)mesh.vertices.size();
    for(size_t i=0;i<tris.size();i+=3){
        if(i+2>=tris.size()) break;
        for(int k=0;k<3;k++){
            Color col = paint(tris[i+k][0], tris[i+k][1]);
            float a = col.a;
            mesh.vertices.push_back({tris[i+k][0],tris[i+k][1],
                col.r*a,col.g*a,col.b*a,a});
            mesh.indices.push_back(base+(uint32_t)(i+k));
        }
    }
}

static std::vector<std::vector<V2>> applyDash(
    const std::vector<V2>& pts, bool closed,
    const std::vector<float>& dashArray, float dashOffset)
{
    if(dashArray.empty() || pts.size() < 2)
        return { pts };

    // close loop
    std::vector<V2> work = pts;
    if(closed) {
        const V2& f = work.front(), &b = work.back();
        if(f[0] != b[0] || f[1] != b[1])
            work.push_back(f);
    }
    int n = (int)work.size();

    float patLen = 0.f;
    for(float d : dashArray) patLen += d;
    if(patLen < 1e-6f) return {};

    // normalise dashOffset into [0, patLen)
    float off = fmodf(dashOffset, patLen);
    if(off < 0.f) off += patLen;

    // find the starting phase (index into dashArray) and how much remains
    int   phaseIdx    = 0;
    float phaseRemain = dashArray[0];
    if(off > 0.f) {
        float rem = off;
        while(rem > 0.f) {
            if(rem < dashArray[phaseIdx]) {
                phaseRemain = dashArray[phaseIdx] - rem;
                break;
            }
            rem -= dashArray[phaseIdx];
            phaseIdx    = (phaseIdx + 1) % (int)dashArray.size();
            phaseRemain = dashArray[phaseIdx];
        }
    }
    bool drawing = (phaseIdx % 2 == 0);

    std::vector<std::vector<V2>> result;
    std::vector<V2> cur;
    if(drawing) cur.push_back(work[0]);

    for(int i = 0; i < n - 1; i++) {
        float dx = work[i+1][0] - work[i][0];
        float dy = work[i+1][1] - work[i][1];
        float segLen = sqrtf(dx*dx + dy*dy);
        if(segLen < 1e-8f) continue;

        float consumed   = 0.f;
        bool  lastWasBdy = false;

        while(consumed < segLen - 1e-6f) {
            float step  = std::min(phaseRemain, segLen - consumed);
            consumed   += step;
            phaseRemain -= step;

            lastWasBdy = (phaseRemain < 1e-6f);
            if(lastWasBdy) {
                float t = consumed / segLen;
                V2 p = { work[i][0] + t * dx, work[i][1] + t * dy };
                if(drawing) {
                    cur.push_back(p);
                    if(cur.size() >= 2) result.push_back(std::move(cur));
                    cur.clear();
                }
                phaseIdx    = (phaseIdx + 1) % (int)dashArray.size();
                phaseRemain = dashArray[phaseIdx];
                drawing     = (phaseIdx % 2 == 0);
                if(drawing) cur.push_back(p);
            }
        }

        // add segment endpoint
        if(drawing && !lastWasBdy)
            cur.push_back(work[i + 1]);
    }

    if(drawing && cur.size() >= 2)
        result.push_back(std::move(cur));
    return result;
}

static void strokeWithDash(Mesh& out,
    const std::vector<V2>& pts, bool closed,
    const Style& st, const PaintEval& paint)
{
    float hw = st.strokeWidth * 0.5f;
    auto segments = applyDash(pts, closed, st.dashArray, st.dashOffset);
    bool segClosed = closed && st.dashArray.empty();
    for(auto& seg : segments) {
        if(seg.size() < 2) continue;
        std::vector<V2> stris;
        appendStrokeContour(stris, seg, hw, segClosed, st.lineCap, st.lineJoin, st.miterLimit);
        appendTriangleStrip(out, stris, paint);
    }
}

static void buildFan(Mesh::StencilFill& sf, const std::vector<V2>& pts) {
    if(pts.size() < 3) return;
    uint32_t base = (uint32_t)sf.verts.size();
    for(auto& p : pts)
        sf.verts.push_back({p[0], p[1], 0.f, 0.f, 0.f, 0.f});
    for(uint32_t i = 1; i + 1 < (uint32_t)pts.size(); i++) {
        sf.indices.push_back(base);
        sf.indices.push_back(base + i);
        sf.indices.push_back(base + i + 1);
    }
}

static void tessellateShape(const SVGShape& shape, Mesh& out,
    const std::unordered_map<std::string,GradientDef>* gradients)
{
    const Style& st  = shape.style;
    const Mat3&  tf  = shape.transform;

    float globalA = st.opacity;
    auto applyTf = [&](std::vector<V2>& pts){
        for(auto& p : pts){
            auto r = tf.apply(p[0],p[1]);
            p={r[0],r[1]};
        }
    };
    auto makeFillPaint = [&](float bx0,float by0,float bw,float bh) {
        return makePaintEval(st.fill, st, gradients, bx0,by0,bw,bh, st.fillOpacity*globalA);
    };
    auto makeStrokePaint = [&](float bx0,float by0,float bw,float bh) {
        return makePaintEval(st.stroke, st, gradients, bx0,by0,bw,bh, st.strokeOpacity*globalA);
    };

#ifdef _DEBUG
    TESSLOG("shape kind=%d  fill_none=%d fill_grad=%d  stroke_none=%d sw=%.1f stroke_grad=%d  opacity=%.2f",
        (int)shape.kind,
        st.fill.none,   st.fill.isGradient(),
        st.stroke.none, st.strokeWidth, st.stroke.isGradient(),
        globalA);
#endif

    switch(shape.kind) {
    case ShapeKind::Rect: {
        std::vector<V2> poly;
        if(shape.rx>0||shape.ry>0)
            poly=makeRoundedRect(shape.x,shape.y,shape.width,shape.height,shape.rx,shape.ry);
        else
            poly={{shape.x,shape.y},{shape.x+shape.width,shape.y},
                  {shape.x+shape.width,shape.y+shape.height},{shape.x,shape.y+shape.height}};
        applyTf(poly);
        float bx,by,bw,bh; computeBounds(poly,bx,by,bw,bh); bw-=bx; bh-=by;
        if(!st.fill.none) {
            PaintEval fp = makeFillPaint(bx,by,bw,bh);
            if(paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, poly, {bx+bw*0.5f, by+bh*0.5f}, fp);
            else
                appendFill(out, poly, fp);
        }
        if(!st.stroke.none)
            strokeWithDash(out, poly, /*closed=*/true, st, makeStrokePaint(bx,by,bw,bh));
        break;
    }
    case ShapeKind::Circle: {
        auto poly=makeEllipse(shape.cx,shape.cy,shape.r,shape.r);
        applyTf(poly);
        float bx,by,bw,bh; computeBounds(poly,bx,by,bw,bh); bw-=bx; bh-=by;
        if(!st.fill.none) {
            PaintEval fp = makeFillPaint(bx,by,bw,bh);
            if(paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, poly, {bx+bw*0.5f, by+bh*0.5f}, fp);
            else
                appendFill(out, poly, fp);
        }
        if(!st.stroke.none)
            strokeWithDash(out, poly, /*closed=*/true, st, makeStrokePaint(bx,by,bw,bh));
        break;
    }
    case ShapeKind::Ellipse: {
        auto poly=makeEllipse(shape.cx,shape.cy,shape.rx,shape.ry);
        applyTf(poly);
        float bx,by,bw,bh; computeBounds(poly,bx,by,bw,bh); bw-=bx; bh-=by;
        if(!st.fill.none) {
            PaintEval fp = makeFillPaint(bx,by,bw,bh);
            if(paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, poly, {bx+bw*0.5f, by+bh*0.5f}, fp);
            else
                appendFill(out, poly, fp);
        }
        if(!st.stroke.none)
            strokeWithDash(out, poly, /*closed=*/true, st, makeStrokePaint(bx,by,bw,bh));
        break;
    }
    case ShapeKind::Line: {
        if(!st.stroke.none){
            std::vector<V2> ln={{shape.x1,shape.y1},{shape.x2,shape.y2}};
            applyTf(ln);
            float bx,by,bw,bh; computeBounds(ln,bx,by,bw,bh); bw-=bx; bh-=by;
            strokeWithDash(out, ln, /*closed=*/false, st, makeStrokePaint(bx,by,bw,bh));
        }
        break;
    }
    case ShapeKind::Polyline: {
        auto pts=shape.points;
        applyTf(pts);
        float bx,by,bw,bh; computeBounds(pts,bx,by,bw,bh); bw-=bx; bh-=by;
        if(!st.fill.none && pts.size()>=3) {
            PaintEval fp = makeFillPaint(bx,by,bw,bh);
            if(paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, pts, {bx+bw*0.5f, by+bh*0.5f}, fp);
            else
                appendFill(out, pts, fp);
        }
        if(!st.stroke.none)
            strokeWithDash(out, pts, /*closed=*/false, st, makeStrokePaint(bx,by,bw,bh));
        break;
    }
    case ShapeKind::Polygon: {
        auto pts=shape.points;
        applyTf(pts);
        float bx,by,bw,bh; computeBounds(pts,bx,by,bw,bh); bw-=bx; bh-=by;
        if(!st.fill.none && pts.size()>=3) {
            PaintEval fp = makeFillPaint(bx,by,bw,bh);
            if(paintIsRadialGradient(st.fill, gradients))
                appendFillRadial(out, pts, {bx+bw*0.5f, by+bh*0.5f}, fp);
            else
                appendFill(out, pts, fp);
        }
        if(!st.stroke.none)
            strokeWithDash(out, pts, /*closed=*/true, st, makeStrokePaint(bx,by,bw,bh));
        break;
    }
    case ShapeKind::Path: {
        auto rawContours = pathToContours(shape.path);
        std::vector<std::vector<V2>> rings;
        float minX= 1e30f, minY= 1e30f, maxX=-1e30f, maxY=-1e30f;
        for(auto& c : rawContours) {
            auto pts = c.pts;
            applyTf(pts);
            if(pts.size() >= 2) {
                for(auto& p : pts) {
                    minX = std::min(minX, p[0]);  minY = std::min(minY, p[1]);
                    maxX = std::max(maxX, p[0]);  maxY = std::max(maxY, p[1]);
                }
            }
            rings.push_back(std::move(pts));
        }
        bool haveBounds = (minX <= maxX);
        float bw = haveBounds ? maxX-minX : 0.f, bh = haveBounds ? maxY-minY : 0.f;

        if(!st.fill.none) {
            std::vector<std::vector<V2>> fillRings;
            for(auto& r : rings) if(r.size() >= 3) fillRings.push_back(r);

            if(fillRings.size() == 1) {
                PaintEval fp = makeFillPaint(minX,minY,bw,bh);
                if(paintIsRadialGradient(st.fill, gradients))
                    appendFillRadial(out, fillRings[0], {minX+bw*0.5f, minY+bh*0.5f}, fp);
                else
                    appendFill(out, fillRings[0], fp);
            } else if(fillRings.size() > 1) {
                Mesh::StencilFill sf;
                sf.evenOdd = (st.fillRule == FillRule::EvenOdd);

                for(auto& ring : fillRings)
                    buildFan(sf, ring);

                PaintEval paint = makeFillPaint(minX,minY,bw,bh);
                sf.bboxBase = (uint32_t)sf.verts.size();
                auto pushCorner = [&](float x,float y){
                    Color c = paint(x,y); float a=c.a;
                    sf.verts.push_back({x,y, c.r*a,c.g*a,c.b*a,a});
                };
                pushCorner(minX,minY); pushCorner(maxX,minY);
                pushCorner(maxX,maxY); pushCorner(minX,maxY);

                sf.indices.push_back(sf.bboxBase);
                sf.indices.push_back(sf.bboxBase + 1);
                sf.indices.push_back(sf.bboxBase + 2);
                sf.indices.push_back(sf.bboxBase + 2);
                sf.indices.push_back(sf.bboxBase + 3);
                sf.indices.push_back(sf.bboxBase);

                out.stencilFills.push_back(std::move(sf));
            }
        }

        if(!st.stroke.none) {
            PaintEval paint = makeStrokePaint(minX,minY,bw,bh);
            for(size_t i = 0; i < rawContours.size(); i++)
                strokeWithDash(out, rings[i], rawContours[i].closed, st, paint);
        }
        break;
    }
    }
}

Mesh tessellateDocument(const SVGDocument& doc) {
    auto startTime = std::chrono::high_resolution_clock::now();

    Mesh m;
    for(auto& shape : doc.shapes)
        tessellateShape(shape, m, &doc.gradients);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    TESSLOG("Tessellation complete: %d vertices, %d indices (%d triangles) in %.2f ms",
        (int)m.vertices.size(), (int)m.indices.size(), (int)m.indices.size()/3, ms);
    return m;
}