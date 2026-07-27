#pragma once
#include "svg_parser_internal.h"
#include "svg_types.h"
#include "xml_mini.h"
#include <string>

void trimStr(std::string &t);
Paint parsePaint(const std::string &val);
void applyDeclarations(const std::string &decls, Style &s);
Style parseStyle(const xml::Node &node, const Style &parent,
                 const StyleSheet &sheet);
void parseStyleSheet(const std::string &css, StyleSheet &sheet);
