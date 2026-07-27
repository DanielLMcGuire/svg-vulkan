#pragma once
#include "svg_parser_internal.h"
#include "svg_types.h"
#include "xml_mini.h"
#include <string>

std::string trim(const std::string &s);
float getFloat(const xml::Node &n, const char *attrName, float percentBasis,
               float def = 0.f);
float pctW(const SVGViewport &vp);
float pctH(const SVGViewport &vp);
float pctDiag(const SVGViewport &vp);

void emitText(const xml::Node &node, const Mat3 &tf, const Style &style,
              const ParseCtx &ctx, std::vector<SVGShape> &out);
