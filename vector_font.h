#pragma once

#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <cctype>

namespace vfont {

using Pt     = std::array<float,2>;
using Stroke = std::vector<Pt>;

struct Glyph {
    float                advance = 0.6f;
    std::vector<Stroke>  strokes;
};

inline Pt G(float x, float y) { return { x/10.f, -y/10.f }; }

inline const std::unordered_map<char, Glyph>& fontTable() {
    static const std::unordered_map<char, Glyph> table = [] {
        std::unordered_map<char, Glyph> f;
        auto add = [&](char c, float advGrid, std::vector<Stroke> gridStrokes) {
            Glyph g;
            g.advance = advGrid / 10.f;
            for (auto& s : gridStrokes) {
                Stroke out;
                out.reserve(s.size());
                for (auto& p : s) out.push_back(G(p[0], p[1]));
                g.strokes.push_back(std::move(out));
            }
            f[c] = std::move(g);
        };

        add(' ', 4, {});
        add('.', 3, {{{2,0},{2,0.4f}}});
        add(',', 3, {{{2,0.6f},{1.3f,-1.2f}}});
        add('-', 5, {{{0.5f,3.5f},{4.5f,3.5f}}});
        add(':', 4, {{{2,4.6f},{2,5.0f}},{{2,1.8f},{2,2.2f}}});
        add(';', 4, {{{2,4.6f},{2,5.0f}},{{2,1.6f},{1.3f,0.2f}}});
        add('!', 3, {{{2,7},{2,2.2f}},{{2,0},{2,0.4f}}});
        add('?', 5, {{{0.3f,5.8f},{1,7},{3,7},{4,6},{4,4.7f},{2,3.5f},{2,2.2f}},{{2,0},{2,0.4f}}});
        add('\'',3, {{{2,7},{2,5.5f}}});
        add('"', 4, {{{1.3f,7},{1.3f,5.5f}},{{2.7f,7},{2.7f,5.5f}}});
        add('/', 5, {{{0,0},{4,7}}});
        add('(', 4, {{{3,7},{1.3f,5.7f},{0.8f,3.5f},{1.3f,1.3f},{3,0}}});
        add(')', 4, {{{1,7},{2.7f,5.7f},{3.2f,3.5f},{2.7f,1.3f},{1,0}}});
        add('+', 5, {{{2,1.5f},{2,5.5f}},{{0,3.5f},{4,3.5f}}});
        add('=', 5, {{{0.5f,2.7f},{3.5f,2.7f}},{{0.5f,4.3f},{3.5f,4.3f}}});
        add('_', 5, {{{0,-0.3f},{4,-0.3f}}});
        add('*', 5, {{{0,2},{4,5}},{{4,2},{0,5}},{{2,1.5f},{2,5.5f}}});

        add('0', 6, {{{2,7},{0,5.5f},{0,1.5f},{2,0},{4,1.5f},{4,5.5f},{2,7}},
                     {{1,1.5f},{3,5.5f}}});
        add('1', 6, {{{1,5.5f},{2,7},{2,0}}});
        add('2', 6, {{{0,5.5f},{0,6},{1,7},{3,7},{4,6},{4,4.5f},{0,0},{4,0}}});
        add('3', 6, {{{0,6.3f},{1,7},{3,7},{4,6},{4,4.3f},{2.5f,3.5f},{4,2.7f},{4,1},{3,0},{1,0},{0,0.7f}}});
        add('4', 6, {{{3,0},{3,7},{0,2},{4,2}}});
        add('5', 6, {{{4,7},{0,7},{0,3.8f},{3,3.8f},{4,3},{4,1},{3,0},{0,0.5f}}});
        add('6', 6, {{{3.5f,7},{1,6},{0,4},{0,1.5f},{1,0},{3,0},{4,1},{4,2.5f},{3,3.5f},{1,3.5f},{0,2.7f}}});
        add('7', 6, {{{0,7},{4,7},{1,0}}});
        add('8', 6, {{{1,3.9f},{0,4.7f},{0,6},{1,7},{3,7},{4,6},{4,4.7f},{1,3.9f}},
                     {{1,3.5f},{0,2.8f},{0,1.2f},{1,0},{3,0},{4,1.2f},{4,2.8f},{1,3.5f}}});
        add('9', 6, {{{0.5f,0},{3,1},{4,3},{4,5.5f},{3,7},{1,7},{0,6},{0,4.5f},{1,3.5f},{3,3.5f},{4,4.3f}}});

        add('A', 6, {{{0,0},{2,7},{4,0}},{{0.8f,2.5f},{3.2f,2.5f}}});
        add('B', 6, {{{0,0},{0,7}},{{0,7},{3,7},{4,6},{4,4.5f},{3,3.5f},{0,3.5f}},
                     {{0,3.5f},{3,3.5f},{4,2.5f},{4,1},{3,0},{0,0}}});
        add('C', 6, {{{4,6},{3,7},{1,7},{0,6},{0,1},{1,0},{3,0},{4,1}}});
        add('D', 6, {{{0,0},{0,7}},{{0,7},{2.5f,7},{4,5.5f},{4,1.5f},{2.5f,0},{0,0}}});
        add('E', 6, {{{4,7},{0,7},{0,0},{4,0}},{{0,3.5f},{3,3.5f}}});
        add('F', 6, {{{0,0},{0,7},{4,7}},{{0,3.5f},{3,3.5f}}});
        add('G', 6, {{{4,6},{3,7},{1,7},{0,6},{0,1},{1,0},{3,0},{4,1},{4,3},{2.5f,3}}});
        add('H', 6, {{{0,0},{0,7}},{{4,0},{4,7}},{{0,3.5f},{4,3.5f}}});
        add('I', 3, {{{1.5f,0},{1.5f,7}}});
        add('J', 6, {{{4,7},{4,1},{3,0},{1,0},{0,1}}});
        add('K', 6, {{{0,0},{0,7}},{{4,7},{0,3.5f},{4,0}}});
        add('L', 6, {{{0,7},{0,0},{4,0}}});
        add('M', 6, {{{0,0},{0,7},{2,3.5f},{4,7},{4,0}}});
        add('N', 6, {{{0,0},{0,7},{4,0},{4,7}}});
        add('O', 6, {{{2,7},{0,5.5f},{0,1.5f},{2,0},{4,1.5f},{4,5.5f},{2,7}}});
        add('P', 6, {{{0,0},{0,7},{3,7},{4,6},{4,4.5f},{3,3.5f},{0,3.5f}}});
        add('Q', 6, {{{2,7},{0,5.5f},{0,1.5f},{2,0},{4,1.5f},{4,5.5f},{2,7}},{{2.3f,1.8f},{4,0}}});
        add('R', 6, {{{0,0},{0,7},{3,7},{4,6},{4,4.5f},{3,3.5f},{0,3.5f}},{{1.5f,3.5f},{4,0}}});
        add('S', 6, {{{4,6},{3,7},{1,7},{0,6},{0,4.2f},{1,3.5f},{3,3.5f},{4,2.8f},{4,1},{3,0},{1,0},{0,1}}});
        add('T', 6, {{{0,7},{4,7}},{{2,7},{2,0}}});
        add('U', 6, {{{0,7},{0,1.5f},{1.5f,0},{2.5f,0},{4,1.5f},{4,7}}});
        add('V', 6, {{{0,7},{2,0},{4,7}}});
        add('W', 6, {{{0,7},{1,0},{2,3.5f},{3,0},{4,7}}});
        add('X', 6, {{{0,0},{4,7}},{{0,7},{4,0}}});
        add('Y', 6, {{{0,7},{2,3.5f},{4,7}},{{2,3.5f},{2,0}}});
        add('Z', 6, {{{0,7},{4,7},{0,0},{4,0}}});

        return f;
    }();
    return table;
}

inline const Glyph* lookupGlyph(char c) {
    const auto& table = fontTable();
    auto it = table.find(c);
    if (it != table.end()) return &it->second;
    if (std::isalpha((unsigned char)c)) {
        it = table.find((char)std::toupper((unsigned char)c));
        if (it != table.end()) return &it->second;
    }
    return nullptr;
}

inline float measureText(const std::string& text) {
    float w = 0.f;
    for (char c : text) {
        if (const Glyph* g = lookupGlyph(c)) w += g->advance;
    }
    return w;
}

inline std::vector<Stroke> layoutText(const std::string& text, float x, float y,
                                       float fontSize, const std::string& anchor)
{
    std::vector<Stroke> out;
    float totalW = measureText(text) * fontSize;
    float startX = x;
    if (anchor == "middle") startX -= totalW * 0.5f;
    else if (anchor == "end") startX -= totalW;

    float penX = startX;
    for (char c : text) {
        const Glyph* g = lookupGlyph(c);
        if (!g) continue;
        for (auto& stroke : g->strokes) {
            Stroke s;
            s.reserve(stroke.size());
            for (auto& p : stroke)
                s.push_back({ penX + p[0]*fontSize, y + p[1]*fontSize });
            out.push_back(std::move(s));
        }
        penX += g->advance * fontSize;
    }
    return out;
}

} // namespace vfont
