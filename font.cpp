#include "font.h"
#include <cstdio>
#include <cstring>
#include <mutex>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace vfont {

namespace {

struct LoadedFont {
    bool                 ok = false;
    std::vector<uint8_t> data;
    stbtt_fontinfo        info{};
    float                 unitsPerEm = 1000.f;
};

LoadedFont& font() {
    static LoadedFont f;
    static std::once_flag once;
    std::call_once(once, [] {
        static const char* candidates[] = {
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\calibri.ttf",
            "C:\\Windows\\Fonts\\tahoma.ttf",
            "C:\\Windows\\Fonts\\verdana.ttf",
            "C:\\Windows\\Fonts\\times.ttf",
            "C:\\Windows\\Fonts\\consola.ttf",
            "/System/Library/Fonts/SFNS.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/Library/Fonts/Arial.ttf",
            "/Library/Fonts/Verdana.ttf",
            "/System/Library/Fonts/Menlo.ttc",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf"
        };
        for(const char* path : candidates) {
            FILE* fp = fopen(path, "rb");
            if(!fp) continue;
            fseek(fp, 0, SEEK_END);
            long len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if(len <= 0) { fclose(fp); continue; }
            std::vector<uint8_t> buf((size_t)len);
            size_t nread = fread(buf.data(), 1, (size_t)len, fp);
            fclose(fp);
            if(nread != (size_t)len) continue;

            stbtt_fontinfo info{};
            if(!stbtt_InitFont(&info, buf.data(), stbtt_GetFontOffsetForIndex(buf.data(), 0)))
                continue;

            f.data = std::move(buf);
            f.info = info;
            int ascent=0, descent=0, lineGap=0;
            stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &lineGap);
            float s = stbtt_ScaleForMappingEmToPixels(&f.info, 1.0f);
            f.unitsPerEm = (s > 1e-8f) ? (1.0f / s) : 1000.f;
            f.ok = true;
            break;
        }
    });
    return f;
}

inline void toLocal(float gx, float gy, float scale, float penX, float penY,
                     float& outX, float& outY)
{
    outX = penX + gx*scale;
    outY = penY - gy*scale;
}

} // namespace

bool available() { return font().ok; }

float appendGlyphPath(std::vector<PathSegment>& out, uint32_t codepoint,
                      float penX, float penY, float fontSize)
{
    LoadedFont& f = font();
    if(!f.ok) return 0.f;
    float scale = fontSize / f.unitsPerEm;

    int advanceRaw = 0, lsbRaw = 0;
    stbtt_GetCodepointHMetrics(&f.info, (int)codepoint, &advanceRaw, &lsbRaw);

    stbtt_vertex* verts = nullptr;
    int nverts = stbtt_GetCodepointShape(&f.info, (int)codepoint, &verts);
    bool haveOpenContour = false;
    for(int i = 0; i < nverts; i++) {
        const stbtt_vertex& v = verts[i];
        PathSegment seg;
        float x, y;
        switch(v.type) {
        case STBTT_vmove:
            if(haveOpenContour) {
                PathSegment close; close.cmd = PathCmd::ClosePath;
                out.push_back(close);
            }
            toLocal(v.x, v.y, scale, penX, penY, x, y);
            seg.cmd = PathCmd::MoveTo;
            seg.args[0] = x; seg.args[1] = y;
            out.push_back(seg);
            haveOpenContour = true;
            break;
        case STBTT_vline:
            toLocal(v.x, v.y, scale, penX, penY, x, y);
            seg.cmd = PathCmd::LineTo;
            seg.args[0] = x; seg.args[1] = y;
            out.push_back(seg);
            break;
        case STBTT_vcurve: {
            float cx, cy, ex, ey;
            toLocal(v.cx, v.cy, scale, penX, penY, cx, cy);
            toLocal(v.x,  v.y,  scale, penX, penY, ex, ey);
            seg.cmd = PathCmd::QuadTo;
            seg.args[0]=cx; seg.args[1]=cy; seg.args[2]=ex; seg.args[3]=ey;
            seg.args[4]=0.f;
            out.push_back(seg);
            break;
        }
        case STBTT_vcubic: {
            float cx1, cy1, cx2, cy2, ex, ey;
            toLocal(v.cx,  v.cy,  scale, penX, penY, cx1, cy1);
            toLocal(v.cx1, v.cy1, scale, penX, penY, cx2, cy2);
            toLocal(v.x,   v.y,   scale, penX, penY, ex,  ey);
            seg.cmd = PathCmd::CubicTo;
            seg.args[0]=cx1; seg.args[1]=cy1; seg.args[2]=cx2; seg.args[3]=cy2;
            seg.args[4]=ex;  seg.args[5]=ey;  seg.args[6]=0.f;
            out.push_back(seg);
            break;
        }
        }
    }
    if(haveOpenContour) {
        PathSegment close; close.cmd = PathCmd::ClosePath;
        out.push_back(close);
    }
    if(verts) stbtt_FreeShape(&f.info, verts);

    return advanceRaw * scale;
}

float glyphAdvance(uint32_t codepoint, float fontSize) {
    LoadedFont& f = font();
    if(!f.ok) return 0.f;
    float scale = fontSize / f.unitsPerEm;
    int advanceRaw = 0, lsbRaw = 0;
    stbtt_GetCodepointHMetrics(&f.info, (int)codepoint, &advanceRaw, &lsbRaw);
    return advanceRaw * scale;
}

float kernAdvance(uint32_t cp1, uint32_t cp2, float fontSize) {
    LoadedFont& f = font();
    if(!f.ok) return 0.f;
    float scale = fontSize / f.unitsPerEm;
    return stbtt_GetCodepointKernAdvance(&f.info, (int)cp1, (int)cp2) * scale;
}

uint32_t utf8Decode(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i];
    if(c < 0x80) { i += 1; return c; }

    int extra; uint32_t cp;
    if((c & 0xE0) == 0xC0)      { extra = 1; cp = c & 0x1Fu; }
    else if((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; }
    else if((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; }
    else { i += 1; return 0xFFFD; }

    if(i + (size_t)extra >= s.size()) { i += 1; return 0xFFFD; }

    uint32_t out = cp;
    for(int k = 1; k <= extra; k++) {
        unsigned char cc = (unsigned char)s[i+k];
        if((cc & 0xC0) != 0x80) { i += 1; return 0xFFFD; }
        out = (out << 6) | (cc & 0x3Fu);
    }
    i += 1 + (size_t)extra;
    return out;
}

} // namespace vfont
