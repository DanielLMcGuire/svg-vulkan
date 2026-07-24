#pragma once
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <cmath>
#include <cstdint>

struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
    static Color fromHex(uint32_t hex) {
        return { ((hex >> 24) & 0xFF) / 255.f,
                 ((hex >> 16) & 0xFF) / 255.f,
                 ((hex >>  8) & 0xFF) / 255.f,
                 ((hex      ) & 0xFF) / 255.f };
    }
    static Color none() { return {0,0,0,0}; }
};

struct Mat3 {
    float m[3][3] = {{1,0,0},{0,1,0},{0,0,1}};

    static Mat3 identity() { return {}; }
    static Mat3 translate(float tx, float ty) {
        Mat3 r; r.m[2][0]=tx; r.m[2][1]=ty; return r;
    }
    static Mat3 scale(float sx, float sy) {
        Mat3 r; r.m[0][0]=sx; r.m[1][1]=sy; return r;
    }
    static Mat3 rotate(float radians) {
        Mat3 r;
        float c = cosf(radians), s = sinf(radians);
        r.m[0][0]= c; r.m[0][1]= s;
        r.m[1][0]=-s; r.m[1][1]= c;
        return r;
    }
    Mat3 operator*(const Mat3& b) const {
        Mat3 out;
        for(int i=0;i<3;i++) for(int j=0;j<3;j++){
            out.m[i][j]=0;
            for(int k=0;k<3;k++) out.m[i][j]+=m[i][k]*b.m[k][j];
        }
        return out;
    }
    std::array<float,2> apply(float x, float y) const {
        return { m[0][0]*x + m[1][0]*y + m[2][0],
                 m[0][1]*x + m[1][1]*y + m[2][1] };
    }
};

enum class PathCmd {
    MoveTo, LineTo, HLineTo, VLineTo,
    CubicTo, QuadTo, ArcTo, ClosePath
};

struct PathSegment {
    PathCmd cmd;
    bool    relative = false;
    float   args[7]  = {};
};

struct Paint {
    bool  none  = true;
    Color color = {};
    static Paint solid(Color c)  { return {false, c}; }
    static Paint transparent()   { return {true,  {}}; }
};

enum class LineCap  { Butt, Round, Square };
enum class LineJoin { Miter, Round, Bevel };
enum class FillRule { NonZero, EvenOdd };

struct Style {
    Paint    fill        = Paint::solid({0,0,0,1});
    Paint    stroke      = Paint::transparent();
    float    strokeWidth = 1.f;
    float    fillOpacity = 1.f;
    float    opacity     = 1.f;
    LineCap  lineCap     = LineCap::Butt;
    LineJoin lineJoin    = LineJoin::Miter;
    float    miterLimit  = 4.f;
    FillRule fillRule    = FillRule::NonZero;

    // note for later: odd number of values should duplicated (SVG spec SS11.4).
    std::vector<float> dashArray;
    float              dashOffset = 0.f;
};

enum class ShapeKind { Path, Rect, Circle, Ellipse, Line, Polygon, Polyline };

struct SVGShape {
    ShapeKind             kind;
    Style                 style;
    Mat3                  transform = Mat3::identity();

    std::vector<PathSegment> path;

    float rx=0,ry=0,x=0,y=0,width=0,height=0;

    float cx=0,cy=0,r=0;

    float x1=0,y1=0,x2=0,y2=0;

    std::vector<std::array<float,2>> points;
};

struct SVGViewport {
    float x=0,y=0,w=800,h=600;
    float vw=800,vh=600;
};

struct SVGDocument {
    SVGViewport           viewport;
    std::vector<SVGShape> shapes;
    std::string           title;
};
