#include "svg_text.h"
#include "font.h"
#include "svg_color.h"
#include "svg_parser_internal.h"
#include "svg_style.h"
#include "svg_transform.h"
#include <cctype>
#include <cmath>
#include <cstdlib>

std::string trim(const std::string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

float getFloat(const xml::Node &n, const char *attrName, float percentBasis,
               float def)
{
    if (auto *v = n.attr(attrName))
        return parseLength(*v, percentBasis);
    return def;
}

float pctW(const SVGViewport &vp) { return vp.w; }
float pctH(const SVGViewport &vp) { return vp.h; }
float pctDiag(const SVGViewport &vp)
{
    return sqrtf(vp.w * vp.w + vp.h * vp.h) * 0.70710678f;
}

static std::string collapseWhitespace(const std::string &s)
{
    std::string out;
    bool lastSpace = false;
    for (char c : s)
    {
        bool isws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (isws)
        {
            if (!lastSpace && !out.empty())
                out += ' ';
            lastSpace = true;
        }
        else
        {
            out += c;
            lastSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

static std::vector<float> parseLengthList(const char *s, float percentOf)
{
    std::vector<float> out;
    if (!s)
        return out;
    const char *p = s;
    while (*p)
    {
        while (*p && (isspace((unsigned char)*p) || *p == ','))
            ++p;
        if (!*p)
            break;
        char *end;
        strtof(p, &end);
        if (end == p)
            break;
        std::string tok(p, (const char *)end);
        while (*end && !isspace((unsigned char)*end) && *end != ',')
            tok += *end++;
        out.push_back(parseLength(tok, percentOf));
        p = end;
    }
    return out;
}

static void shiftSegCoord(PathSegment &s, float dx, float dy)
{
    switch (s.cmd)
    {
    case PathCmd::MoveTo:
    case PathCmd::LineTo:
        s.args[0] += dx;
        s.args[1] += dy;
        break;
    case PathCmd::QuadTo:
        s.args[0] += dx;
        s.args[1] += dy;
        s.args[2] += dx;
        s.args[3] += dy;
        break;
    case PathCmd::CubicTo:
        s.args[0] += dx;
        s.args[1] += dy;
        s.args[2] += dx;
        s.args[3] += dy;
        s.args[4] += dx;
        s.args[5] += dy;
        break;
    default:
        break;
    }
}

static std::array<float, 2>
layoutTextRun(const std::string &text, float startX, float startY,
              const std::vector<float> &xs, const std::vector<float> &ys,
              const std::vector<float> &dxs, const std::vector<float> &dys,
              const Style &style, std::vector<PathSegment> &segsOut)
{
    float penX = xs.empty() ? startX : xs[0];
    float penY = ys.empty() ? startY : ys[0];
    if (text.empty() || style.fontSize <= 0.f || !vfont::available())
        return {penX, penY};

    std::vector<PathSegment> chunk;
    float chunkStartX = penX;
    uint32_t prevCp = 0;
    bool havePrev = false;
    size_t charIdx = 0, byteIdx = 0;

    auto flushChunk = [&](float endX)
    {
        float width = endX - chunkStartX;
        float shift = (style.textAnchor == "middle") ? -width * 0.5f
                      : (style.textAnchor == "end")  ? -width
                                                     : 0.f;
        for (auto &s : chunk)
            shiftSegCoord(s, shift, 0.f);
        segsOut.insert(segsOut.end(), chunk.begin(), chunk.end());
        chunk.clear();
    };

    while (byteIdx < text.size())
    {
        bool isChunkStart =
            (charIdx == 0) || (charIdx < xs.size()) || (charIdx < ys.size());
        if (isChunkStart && charIdx > 0)
        {
            flushChunk(penX);
            chunkStartX = (charIdx < xs.size()) ? xs[charIdx] : penX;
            havePrev = false;
        }
        if (charIdx < xs.size())
            penX = xs[charIdx];
        if (charIdx < ys.size())
            penY = ys[charIdx];

        uint32_t cp = vfont::utf8Decode(text, byteIdx);

        if (havePrev && !isChunkStart)
            penX += vfont::kernAdvance(prevCp, cp, style.fontSize);
        if (charIdx < dxs.size())
            penX += dxs[charIdx];
        if (charIdx < dys.size())
            penY += dys[charIdx];

        float advance =
            vfont::appendGlyphPath(chunk, cp, penX, penY, style.fontSize);
        penX += advance;
        prevCp = cp;
        havePrev = true;
        charIdx++;
    }
    flushChunk(penX);
    return {penX, penY};
}

static bool stylesEqualForText(const Style &a, const Style &b)
{
    auto paintEq = [](const Paint &x, const Paint &y)
    {
        if (x.none != y.none)
            return false;
        if (x.none)
            return true;
        if (x.useCurrentColor != y.useCurrentColor)
            return false;
        if (x.gradientId != y.gradientId)
            return false;
        if (x.useCurrentColor || x.isGradient())
            return true;
        return x.color.r == y.color.r && x.color.g == y.color.g &&
               x.color.b == y.color.b && x.color.a == y.color.a;
    };
    return a.fontSize == b.fontSize && a.textAnchor == b.textAnchor &&
           a.fillOpacity == b.fillOpacity &&
           a.strokeOpacity == b.strokeOpacity && a.opacity == b.opacity &&
           a.strokeWidth == b.strokeWidth && a.fillRule == b.fillRule &&
           a.lineCap == b.lineCap && a.lineJoin == b.lineJoin &&
           a.miterLimit == b.miterLimit && a.dashArray == b.dashArray &&
           a.dashOffset == b.dashOffset &&
           a.currentColor.r == b.currentColor.r &&
           a.currentColor.g == b.currentColor.g &&
           a.currentColor.b == b.currentColor.b &&
           a.currentColor.a == b.currentColor.a && paintEq(a.fill, b.fill) &&
           paintEq(a.stroke, b.stroke);
}

void emitText(const xml::Node &node, const Mat3 &tf, const Style &style,
              const ParseCtx &ctx, std::vector<SVGShape> &out)
{
    auto xs = parseLengthList(
        node.attr("x") ? node.attr("x")->c_str() : nullptr, pctW(*ctx.vp));
    auto ys = parseLengthList(
        node.attr("y") ? node.attr("y")->c_str() : nullptr, pctH(*ctx.vp));
    auto dxs = parseLengthList(
        node.attr("dx") ? node.attr("dx")->c_str() : nullptr, pctW(*ctx.vp));
    auto dys = parseLengthList(
        node.attr("dy") ? node.attr("dy")->c_str() : nullptr, pctH(*ctx.vp));

    std::vector<PathSegment> segs;
    std::string mainText = collapseWhitespace(node.text);
    if (!mainText.empty() && mainText.front() == ' ')
        mainText.erase(mainText.begin());

    auto pen = layoutTextRun(mainText, 0.f, 0.f, xs, ys, dxs, dys, style, segs);

    for (auto &child : node.children)
    {
        if (child.tag != "tspan")
            continue;

        Style spanStyle = parseStyle(child, style, *ctx.sheet);
        Mat3 spanTf = tf;
        if (auto *t = child.attr("transform"))
            spanTf = tf * parseTransform(*t);
        bool needsOwnShape = (child.attr("transform") != nullptr);

        auto sxs = parseLengthList(child.attr("x") ? child.attr("x")->c_str()
                                                   : nullptr,
                                   pctW(*ctx.vp));
        auto sys = parseLengthList(child.attr("y") ? child.attr("y")->c_str()
                                                   : nullptr,
                                   pctH(*ctx.vp));
        auto sdxs = parseLengthList(child.attr("dx") ? child.attr("dx")->c_str()
                                                     : nullptr,
                                    pctW(*ctx.vp));
        auto sdys = parseLengthList(child.attr("dy") ? child.attr("dy")->c_str()
                                                     : nullptr,
                                    pctH(*ctx.vp));

        std::string spanText = collapseWhitespace(child.text);
        if (!spanText.empty() && spanText.front() == ' ')
            spanText.erase(spanText.begin());
        if (spanText.empty())
        {
            if (!sxs.empty())
                pen[0] = sxs[0];
            if (!sys.empty())
                pen[1] = sys[0];
            continue;
        }

        if (needsOwnShape)
        {
            std::vector<PathSegment> spanSegs;
            layoutTextRun(spanText, pen[0], pen[1], sxs, sys, sdxs, sdys,
                          spanStyle, spanSegs);
            if (!spanSegs.empty())
            {
                SVGShape s;
                s.kind = ShapeKind::Path;
                s.style = spanStyle;
                s.transform = spanTf;
                s.path = std::move(spanSegs);
                out.push_back(std::move(s));
            }
        }
        else if (stylesEqualForText(spanStyle, style))
        {
            pen = layoutTextRun(spanText, pen[0], pen[1], sxs, sys, sdxs, sdys,
                                spanStyle, segs);
        }
        else
        {
            std::vector<PathSegment> spanSegs;
            pen = layoutTextRun(spanText, pen[0], pen[1], sxs, sys, sdxs, sdys,
                                spanStyle, spanSegs);
            if (!spanSegs.empty())
            {
                SVGShape s;
                s.kind = ShapeKind::Path;
                s.style = spanStyle;
                s.transform = spanTf;
                s.path = std::move(spanSegs);
                out.push_back(std::move(s));
            }
        }
    }

    if (!segs.empty())
    {
        SVGShape s;
        s.kind = ShapeKind::Path;
        s.style = style;
        s.transform = tf;
        s.path = std::move(segs);
        out.push_back(std::move(s));
    }
}
