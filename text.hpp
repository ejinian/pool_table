#ifndef TEXT_HPP
#define TEXT_HPP

#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// Text — stb_truetype-based glyph renderer.
//
// Startup:
//   Text::init("/System/Library/Fonts/HelveticaNeue.ttc");
//
// Ball decal:
//   GLuint t = Text::makeGlyphTex('8', 128);   // once at startup
//   Text::drawGlyphQuad(t, w, h);               // in ball local-space
//   glDeleteTextures(1, &t);                    // at shutdown
//
// HUD (call between Render::beginHUD / endHUD):
//   glColor3f(1,1,1);
//   Text::drawString("Score: 3", 20, 60, 24);
// ---------------------------------------------------------------------------
namespace Text {

static stbtt_fontinfo   g_font;
static std::vector<unsigned char> g_fontBuf;
static bool             g_ready = false;

// Try each path in order; return true on first success.
inline bool init(std::initializer_list<const char*> paths, int fontIndex = 0) {
    for (const char* path : paths) {
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f); rewind(f);
        g_fontBuf.resize(sz);
        fread(g_fontBuf.data(), 1, sz, f);
        fclose(f);
        int off = stbtt_GetFontOffsetForIndex(g_fontBuf.data(), fontIndex);
        if (off < 0) { g_fontBuf.clear(); continue; }
        if (!stbtt_InitFont(&g_font, g_fontBuf.data(), off)) { g_fontBuf.clear(); continue; }
        g_ready = true;
        printf("[Text] Loaded font: %s\n", path);
        return true;
    }
    fprintf(stderr, "[Text] WARNING: could not load any font — glyphs will be blank.\n");
    return false;
}

inline bool ready() { return g_ready; }

// ---------------------------------------------------------------------------
// Rasterize a single character to an OpenGL alpha texture.
// The glyph is centred in a texSize×texSize bitmap.
// Returns 0 on failure. Caller must glDeleteTextures when done.
inline GLuint makeGlyphTex(char ch, int texSize = 128) {
    if (!g_ready) return 0;

    float scale = stbtt_ScaleForPixelHeight(&g_font, texSize * 0.78f);
    int w, h, xoff, yoff;
    unsigned char* bm = stbtt_GetCodepointBitmap(
        &g_font, scale, scale, (unsigned char)ch, &w, &h, &xoff, &yoff);
    if (!bm) return 0;

    std::vector<unsigned char> tex(texSize * texSize, 0);
    int x0 = (texSize - w) / 2;
    int y0 = (texSize - h) / 2;
    for (int row = 0; row < h; row++) {
        int dy = y0 + row;
        if (dy < 0 || dy >= texSize) continue;
        for (int col = 0; col < w; col++) {
            int dx = x0 + col;
            if (dx < 0 || dx >= texSize) continue;
            tex[dy * texSize + dx] = bm[row * w + col];
        }
    }
    stbtt_FreeBitmap(bm, nullptr);

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, texSize, texSize, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, tex.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

// ---------------------------------------------------------------------------
// Draw a glyph texture as a quad centred at origin in the current matrix.
// w, h: world-space size of the quad.
// Call with lighting OFF, blending ON, color set to desired glyph color.
inline void drawGlyphQuad(GLuint texId, float w, float h) {
    if (!texId) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texId);
    float hw = w * 0.5f, hh = h * 0.5f;
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 1.f); glVertex3f(-hw, -hh, 0.f);
    glTexCoord2f(1.f, 1.f); glVertex3f( hw, -hh, 0.f);
    glTexCoord2f(1.f, 0.f); glVertex3f( hw,  hh, 0.f);
    glTexCoord2f(0.f, 0.f); glVertex3f(-hw,  hh, 0.f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// ---------------------------------------------------------------------------
// Draw a string in current 2-D screen space (after Render::beginHUD).
// x, y: bottom-left anchor in pixels.  pixH: target cap-height in pixels.
// Rasterizes glyphs on the fly — fine for low-frequency UI text.
inline void drawString(const char* str, float x, float y, float pixH) {
    if (!g_ready || !str || !*str) return;

    float scale = stbtt_ScaleForPixelHeight(&g_font, pixH);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &lineGap);
    float baseline = y + ascent * scale;

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float cx = x;
    for (const char* p = str; *p; p++) {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&g_font, *p, &advance, &lsb);

        int bx0, by0, bx1, by1;
        stbtt_GetCodepointBitmapBox(&g_font, *p, scale, scale,
                                    &bx0, &by0, &bx1, &by1);
        int gw = bx1 - bx0, gh = by1 - by0;
        if (gw > 0 && gh > 0) {
            std::vector<unsigned char> bm(gw * gh);
            stbtt_MakeCodepointBitmap(&g_font, bm.data(), gw, gh, gw,
                                      scale, scale, *p);
            GLuint id;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, gw, gh, 0,
                         GL_ALPHA, GL_UNSIGNED_BYTE, bm.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            float gx = cx + bx0;
            float gy = baseline + by0;
            glBegin(GL_QUADS);
            glTexCoord2f(0,0); glVertex2f(gx,      gy);
            glTexCoord2f(1,0); glVertex2f(gx + gw, gy);
            glTexCoord2f(1,1); glVertex2f(gx + gw, gy + gh);
            glTexCoord2f(0,1); glVertex2f(gx,      gy + gh);
            glEnd();

            glDeleteTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        cx += advance * scale;
        if (*(p + 1))
            cx += stbtt_GetCodepointKernAdvance(&g_font, *p, *(p + 1)) * scale;
    }

    glDisable(GL_TEXTURE_2D);
}

} // namespace Text
#endif
