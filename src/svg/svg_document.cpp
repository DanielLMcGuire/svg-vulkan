#include "svg_color.h"
#include "svg_parser.h"
#include "svg_parser_internal.h"
#include "svg_path.h"
#include "svg_style.h"
#include "svg_text.h"
#include "svg_transform.h"
#include "xml_mini.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

static bool isNonRenderingContainer(const std::string &tag)
{
    static const std::unordered_map<std::string, bool> skip = {
        {"defs", true},
        {"symbol", true},
        {"clipPath", true},
        {"mask", true},
        {"pattern", true},
        {"marker", true},
        {"linearGradient", true},
        {"radialGradient", true},
        {"metadata", true},
        {"desc", true},
        {"title", true},
        {"text", true}, // <text> is handled by emitText
    };
    return skip.count(tag) != 0;
}

static void parseGradientStops(const xml::Node &node,
                               std::vector<GradientStop> &stops,
                               const Style &rootStyle)
{
    for (auto &child : node.children)
    {
        if (child.tag != "stop")
            continue;
        GradientStop st;
        std::string offStr = child.attr("offset") ? *child.attr("offset") : "0";
        float v = strtof(offStr.c_str(), nullptr);
        st.offset = (offStr.find('%') != std::string::npos) ? v / 100.f : v;
        st.offset = std::max(0.f, std::min(1.f, st.offset));

        Color col = {0, 0, 0, 1};
        float opa = 1.f;
        auto applyStopDecl = [&](const std::string &key, const std::string &val)
        {
            if (key == "stop-color")
                col = (val == "currentColor") ? rootStyle.currentColor
                      : (val == "none")       ? Color::none()
                                              : parseColor(val);
            else if (key == "stop-opacity")
                opa = strtof(val.c_str(), nullptr);
        };
        if (auto *sc = child.attr("stop-color"))
            applyStopDecl("stop-color", *sc);
        if (auto *so = child.attr("stop-opacity"))
            applyStopDecl("stop-opacity", *so);
        if (auto *sty = child.attr("style"))
        {
            std::istringstream ss(*sty);
            std::string tok;
            while (std::getline(ss, tok, ';'))
            {
                auto c = tok.find(':');
                if (c == std::string::npos)
                    continue;
                std::string k = tok.substr(0, c), val = tok.substr(c + 1);
                trimStr(k);
                trimStr(val);
                applyStopDecl(k, val);
            }
        }
        col.a *= opa;
        st.color = col;
        stops.push_back(st);
    }
}

static void
indexDocument(const xml::Node &node,
              std::unordered_map<std::string, const xml::Node *> &idMap,
              std::unordered_map<std::string, GradientDef> &gradients,
              std::unordered_map<std::string, std::string> &gradHref,
              const Style &rootStyle, const SVGViewport &vp)
{
    if (auto *idAttr = node.attr("id"))
        idMap[*idAttr] = &node;

    if (node.tag == "linearGradient" || node.tag == "radialGradient")
    {
        if (auto *idAttr = node.attr("id"))
        {
            GradientDef g;
            g.kind = (node.tag == "radialGradient") ? GradientKind::Radial
                                                    : GradientKind::Linear;
            if (auto *u = node.attr("gradientUnits"))
                g.userSpaceOnUse = (*u == "userSpaceOnUse");
            if (auto *sm = node.attr("spreadMethod"))
            {
                if (*sm == "reflect")
                    g.spread = SpreadMethod::Reflect;
                else if (*sm == "repeat")
                    g.spread = SpreadMethod::Repeat;
            }
            if (auto *t = node.attr("gradientTransform"))
                g.gradientTransform = parseTransform(*t);

            float bx = g.userSpaceOnUse ? pctW(vp) : 1.f;
            float by = g.userSpaceOnUse ? pctH(vp) : 1.f;
            float bd = g.userSpaceOnUse ? pctDiag(vp) : 1.f;

            if (g.kind == GradientKind::Linear)
            {
                g.x1 =
                    node.attr("x1") ? parseLength(*node.attr("x1"), bx) : 0.f;
                g.y1 =
                    node.attr("y1") ? parseLength(*node.attr("y1"), by) : 0.f;
                g.x2 = node.attr("x2") ? parseLength(*node.attr("x2"), bx)
                                       : (g.userSpaceOnUse ? pctW(vp) : 1.f);
                g.y2 =
                    node.attr("y2") ? parseLength(*node.attr("y2"), by) : 0.f;
            }
            else
            {
                float defC = g.userSpaceOnUse ? 0.5f : 0.5f;
                g.cx = node.attr("cx") ? parseLength(*node.attr("cx"), bx)
                                       : defC * bx;
                g.cy = node.attr("cy") ? parseLength(*node.attr("cy"), by)
                                       : defC * by;
                g.r = node.attr("r") ? parseLength(*node.attr("r"), bd)
                                     : 0.5f * bd;
                g.fx = g.cx;
                g.fy = g.cy;
                if (auto *fx = node.attr("fx"))
                {
                    g.fx = parseLength(*fx, bx);
                    g.hasFocal = true;
                }
                if (auto *fy = node.attr("fy"))
                {
                    g.fy = parseLength(*fy, by);
                    g.hasFocal = true;
                }
            }

            parseGradientStops(node, g.stops, rootStyle);
            if (auto *href = node.attr("href"))
            {
                if (!href->empty() && (*href)[0] == '#')
                    gradHref[*idAttr] = href->substr(1);
            }
            gradients[*idAttr] = std::move(g);
        }
    }

    for (auto &child : node.children)
        indexDocument(child, idMap, gradients, gradHref, rootStyle, vp);
}

static void collectShapes(const xml::Node &node, const Mat3 &parentTf,
                          const Style &parentStyle, const ParseCtx &ctx,
                          std::vector<SVGShape> &out, bool forceDescend = false)
{
    Style nodeStyle = parseStyle(node, parentStyle, *ctx.sheet);
    if (!nodeStyle.display || !nodeStyle.visible)
    {
        SVGLOG("skipping hidden node '%s'", node.tag.c_str());
        return;
    }

    Mat3 nodeTf = parentTf;
    if (auto *t = node.attr("transform"))
        nodeTf = parentTf * parseTransform(*t);

    const SVGViewport &vp = *ctx.vp;
    auto stamp = [&](SVGShape &s)
    {
        s.style = nodeStyle;
        s.transform = nodeTf;
    };

    if (node.tag == "rect")
    {
        SVGShape s;
        stamp(s);
        s.kind = ShapeKind::Rect;
        s.x = getFloat(node, "x", pctW(vp));
        s.y = getFloat(node, "y", pctH(vp));
        s.width = getFloat(node, "width", pctW(vp));
        s.height = getFloat(node, "height", pctH(vp));
        s.rx = getFloat(node, "rx", pctW(vp));
        s.ry = getFloat(node, "ry", pctH(vp));
        if (s.rx > 0 && s.ry == 0)
            s.ry = s.rx;
        if (s.ry > 0 && s.rx == 0)
            s.rx = s.ry;
        SVGLOG("rect x=%.1f y=%.1f w=%.1f h=%.1f fill_none=%d stroke_none=%d",
               s.x, s.y, s.width, s.height, s.style.fill.none,
               s.style.stroke.none);
        out.push_back(s);
    }
    else if (node.tag == "circle")
    {
        SVGShape s;
        stamp(s);
        s.kind = ShapeKind::Circle;
        s.cx = getFloat(node, "cx", pctW(vp));
        s.cy = getFloat(node, "cy", pctH(vp));
        s.r = getFloat(node, "r", pctDiag(vp));
        SVGLOG("circle cx=%.1f cy=%.1f r=%.1f", s.cx, s.cy, s.r);
        out.push_back(s);
    }
    else if (node.tag == "ellipse")
    {
        SVGShape s;
        stamp(s);
        s.kind = ShapeKind::Ellipse;
        s.cx = getFloat(node, "cx", pctW(vp));
        s.cy = getFloat(node, "cy", pctH(vp));
        s.rx = getFloat(node, "rx", pctW(vp));
        s.ry = getFloat(node, "ry", pctH(vp));
        SVGLOG("ellipse cx=%.1f cy=%.1f rx=%.1f ry=%.1f", s.cx, s.cy, s.rx,
               s.ry);
        out.push_back(s);
    }
    else if (node.tag == "line")
    {
        SVGShape s;
        stamp(s);
        s.kind = ShapeKind::Line;
        s.x1 = getFloat(node, "x1", pctW(vp));
        s.y1 = getFloat(node, "y1", pctH(vp));
        s.x2 = getFloat(node, "x2", pctW(vp));
        s.y2 = getFloat(node, "y2", pctH(vp));
        SVGLOG("line (%.1f,%.1f)-(%.1f,%.1f)", s.x1, s.y1, s.x2, s.y2);
        out.push_back(s);
    }
    else if (node.tag == "polyline")
    {
        SVGShape s;
        stamp(s);
        s.kind = ShapeKind::Polyline;
        if (auto *pv = node.attr("points"))
            s.points = parsePoints(*pv);
        SVGLOG("polyline pts=%d", (int)s.points.size());
        out.push_back(s);
    }
    else if (node.tag == "polygon")
    {
        SVGShape s;
        stamp(s);
        s.kind = ShapeKind::Polygon;
        if (auto *pv = node.attr("points"))
            s.points = parsePoints(*pv);
        SVGLOG("polygon pts=%d", (int)s.points.size());
        out.push_back(s);
    }
    else if (node.tag == "path")
    {
        SVGShape s;
        stamp(s);
        s.kind = ShapeKind::Path;
        if (auto *dv = node.attr("d"))
            s.path = parsePath(*dv);
        SVGLOG("path cmds=%d", (int)s.path.size());
        out.push_back(s);
    }
    else if (node.tag == "text")
    {
        emitText(node, nodeTf, nodeStyle, ctx, out);
    }
    else if (node.tag == "use")
    {
        const std::string *href = node.attr("href");
        if (href && !href->empty() && (*href)[0] == '#' && ctx.useDepth < 8)
        {
            std::string id = href->substr(1);
            auto it = ctx.idMap->find(id);
            if (it != ctx.idMap->end() && it->second != &node)
            {
                float ux = getFloat(node, "x", pctW(vp));
                float uy = getFloat(node, "y", pctH(vp));
                Mat3 useTf = nodeTf * Mat3::translate(ux, uy);
                ParseCtx childCtx = ctx;
                childCtx.useDepth = ctx.useDepth + 1;
                SVGLOG("<use> -> #%s at (%.1f,%.1f)", id.c_str(), ux, uy);
                collectShapes(*it->second, useTf, nodeStyle, childCtx, out,
                              true);
            }
            else
            {
                SVGLOG(
                    "WARNING: <use> href '%s' unresolved or self-referential",
                    href->c_str());
            }
        }
    }

    if (forceDescend || !isNonRenderingContainer(node.tag))
    {
        for (auto &child : node.children)
            collectShapes(child, nodeTf, nodeStyle, ctx, out);
    }
}

SVGDocument parseSVG(const std::string &svg, const std::string &filePath)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    SVGDocument doc;
    doc.path = trim(filePath);
    SVGLOG("SVG path: '%s'", doc.path.c_str());
    xml::Node root = xml::parse(svg);

    const xml::Node *svgNode = nullptr;
    std::vector<const xml::Node *> queue = {&root};
    while (!queue.empty() && !svgNode)
    {
        const xml::Node *cur = queue.back();
        queue.pop_back();
        if (cur->tag == "svg")
        {
            svgNode = cur;
            break;
        }
        for (auto &c : cur->children)
            queue.push_back(&c);
    }
    if (!svgNode)
    {
        SVGLOG("ERROR: no <svg> element found");
        return doc;
    }

    for (auto &child : svgNode->children)
    {
        if (child.tag == "title")
        {
            doc.title = trim(child.text);
            SVGLOG("SVG title: '%s'", doc.title.c_str());
            break;
        }
    }

    float vbX = 0, vbY = 0, vbW = 0, vbH = 0;
    bool hasViewBox = false;
    if (auto *vb = svgNode->attr("viewBox"))
        hasViewBox = parseViewBox(*vb, vbX, vbY, vbW, vbH);

    float vw = 0, vh = 0;
    {
        const std::string *ws = svgNode->attr("width");
        const std::string *hs = svgNode->attr("height");
        float vbBase = hasViewBox ? vbW : 0.f;
        float vhBase = hasViewBox ? vbH : 0.f;
        vw = ws ? parseLength(*ws, vbBase) : (hasViewBox ? vbW : 800.f);
        vh = hs ? parseLength(*hs, vhBase) : (hasViewBox ? vbH : 600.f);
        if (vw <= 0)
            vw = hasViewBox ? vbW : 800.f;
        if (vh <= 0)
            vh = hasViewBox ? vbH : 600.f;
    }

    doc.viewport.vw = vw;
    doc.viewport.vh = vh;

    if (hasViewBox)
    {
        doc.viewport.x = vbX;
        doc.viewport.y = vbY;
        doc.viewport.w = vbW;
        doc.viewport.h = vbH;
    }
    else
    {
        doc.viewport.x = 0;
        doc.viewport.y = 0;
        doc.viewport.w = vw;
        doc.viewport.h = vh;
    }

    SVGLOG("SVG viewport: vw=%.0f vh=%.0f  viewBox=(%.0f,%.0f,%.0f,%.0f)",
           doc.viewport.vw, doc.viewport.vh, doc.viewport.x, doc.viewport.y,
           doc.viewport.w, doc.viewport.h);

    StyleSheet sheet;
    {
        const char *p = svg.c_str();
        while (*p)
        {
            const char *found = nullptr;
            for (const char *q = p; *q; ++q)
            {
                if ((q[0] == '<') && (q[1] == 's' || q[1] == 'S') &&
                    (q[2] == 't' || q[2] == 'T') &&
                    (q[3] == 'y' || q[3] == 'Y') &&
                    (q[4] == 'l' || q[4] == 'L') &&
                    (q[5] == 'e' || q[5] == 'E') &&
                    ((unsigned char)q[6] <= 32 || q[6] == '>'))
                {
                    found = q;
                    break;
                }
            }
            if (!found)
                break;
            const char *inner = found + 6;
            while (*inner && *inner != '>')
                ++inner;
            if (*inner == '>')
                ++inner;
            const char *end = inner;
            while (*end)
            {
                if (end[0] == '<' && end[1] == '/')
                {
                    const char *e2 = end + 2;
                    while (*e2 == ' ')
                        ++e2;
                    if ((e2[0] == 's' || e2[0] == 'S') &&
                        (e2[1] == 't' || e2[1] == 'T') &&
                        (e2[2] == 'y' || e2[2] == 'Y') &&
                        (e2[3] == 'l' || e2[3] == 'L') &&
                        (e2[4] == 'e' || e2[4] == 'E'))
                        break;
                }
                ++end;
            }
            std::string cssText(inner, end);
            SVGLOG("Found <style> block (%d chars)", (int)cssText.size());
            parseStyleSheet(cssText, sheet);
            p = (*end ? end + 1 : end);
        }
    }
    SVGLOG("Stylesheet: %d rules", (int)sheet.size());

    Style defaultStyle;
    defaultStyle.fill = Paint::solid({0, 0, 0, 1});
    defaultStyle.stroke = Paint::transparent();
    defaultStyle.strokeWidth = 1.f;

    std::unordered_map<std::string, const xml::Node *> idMap;
    std::unordered_map<std::string, std::string> gradHref;
    indexDocument(*svgNode, idMap, doc.gradients, gradHref, defaultStyle,
                  doc.viewport);

    for (auto &kv : doc.gradients)
    {
        if (!kv.second.stops.empty())
            continue;
        std::string cur = kv.first;
        for (int depth = 0; depth < 8; ++depth)
        {
            auto hit = gradHref.find(cur);
            if (hit == gradHref.end())
                break;
            cur = hit->second;
            auto git = doc.gradients.find(cur);
            if (git == doc.gradients.end())
                break;
            if (!git->second.stops.empty())
            {
                kv.second.stops = git->second.stops;
                break;
            }
        }
    }
    SVGLOG("Indexed %d id'd nodes, %d gradients", (int)idMap.size(),
           (int)doc.gradients.size());

    ParseCtx ctx;
    ctx.vp = &doc.viewport;
    ctx.sheet = &sheet;
    ctx.idMap = &idMap;

    Mat3 rootTf = Mat3::translate(-doc.viewport.x, -doc.viewport.y);

    collectShapes(*svgNode, rootTf, defaultStyle, ctx, doc.shapes);
    auto endTime = std::chrono::high_resolution_clock::now();
    float ms =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();
    SVGLOG("SVG parsed: %d shapes in %.2f ms", (int)doc.shapes.size(), ms);
    return doc;
}