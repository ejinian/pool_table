#ifndef RENDER_HPP
#define RENDER_HPP

#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <cmath>
#include <vector>
#include "vec3.hpp"
#include "physics.hpp"
#include "table.hpp"
#include "text.hpp"

namespace Render {

constexpr float PI = 3.14159265358979f;

static GLuint g_eightTex = 0;

inline void init() {
    Text::init({
        "/System/Library/Fonts/HelveticaNeue.ttc",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Geneva.ttf"
    });
    g_eightTex = Text::makeGlyphTex('8', 128);
}

inline void shutdown() {
    if (g_eightTex) { glDeleteTextures(1, &g_eightTex); g_eightTex = 0; }
}

// ---------------------------------------------------------------------------
inline void drawSphere(float r, int slices = 28, int stacks = 20) {
    for (int i = 0; i < stacks; i++) {
        float la0 = PI * (-0.5f + (float)i / stacks);
        float la1 = PI * (-0.5f + (float)(i + 1) / stacks);
        float y0 = std::sin(la0), y1 = std::sin(la1);
        float c0 = std::cos(la0), c1 = std::cos(la1);
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            float lo = 2 * PI * j / slices, x = std::cos(lo), z = std::sin(lo);
            glNormal3f(x*c0, y0, z*c0); glVertex3f(r*x*c0, r*y0, r*z*c0);
            glNormal3f(x*c1, y1, z*c1); glVertex3f(r*x*c1, r*y1, r*z*c1);
        }
        glEnd();
    }
}

inline void drawDisk(float r, int seg = 32) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= seg; i++) {
        float a = 2 * PI * i / seg;
        glVertex3f(r * std::cos(a), r * std::sin(a), 0);
    }
    glEnd();
}

// Filled spherical cap centered on the +Z pole.
// Uses multiple latitude stacks so the surface curves with the sphere — not a cone.
inline void drawSphericalCap(float r, float capAngle, int stacks = 8, int slices = 48) {
    // Innermost stack: TRIANGLE_FAN from pole to first latitude ring.
    {
        float th = capAngle / stacks;
        float s = std::sin(th), c = std::cos(th);
        glBegin(GL_TRIANGLE_FAN);
        glNormal3f(0.f, 0.f, 1.f); glVertex3f(0.f, 0.f, r);
        for (int j = 0; j <= slices; j++) {
            float phi = 2.f * PI * j / slices;
            float cp = std::cos(phi), sp = std::sin(phi);
            glNormal3f(s*cp, s*sp, c); glVertex3f(r*s*cp, r*s*sp, r*c);
        }
        glEnd();
    }
    // Remaining stacks: QUAD_STRIP between adjacent latitude rings.
    for (int i = 1; i < stacks; i++) {
        float th0 = capAngle * i / stacks,       th1 = capAngle * (i+1) / stacks;
        float s0  = std::sin(th0), c0 = std::cos(th0);
        float s1  = std::sin(th1), c1 = std::cos(th1);
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            float phi = 2.f * PI * j / slices;
            float cp = std::cos(phi), sp = std::sin(phi);
            glNormal3f(s0*cp, s0*sp, c0); glVertex3f(r*s0*cp, r*s0*sp, r*c0);
            glNormal3f(s1*cp, s1*sp, c1); glVertex3f(r*s1*cp, r*s1*sp, r*c1);
        }
        glEnd();
    }
}

// Same multi-stack cap geometry, with the "8" glyph projected orthographically.
// UV: u = (sinθ/sinCapAngle)*cosφ*0.5+0.5, v = -(sinθ/sinCapAngle)*sinφ*0.5+0.5
inline void drawSphericalCapTextured(GLuint texId, float r, float capAngle,
                                     int stacks = 8, int slices = 48) {
    if (!texId) return;
    float sinC = std::sin(capAngle);

    // t = sinθ/sinC maps [0,capAngle]→[0,1]; u/v from orthographic projection.
    auto texUV = [&](float theta, float phi, float& u, float& v) {
        float t = std::sin(theta) / sinC;
        u =  t * std::cos(phi) * 0.5f + 0.5f;
        v = -t * std::sin(phi) * 0.5f + 0.5f;
    };

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texId);

    // Innermost stack: TRIANGLE_FAN from pole.
    {
        float th = capAngle / stacks;
        float s = std::sin(th), c = std::cos(th);
        glBegin(GL_TRIANGLE_FAN);
        glNormal3f(0.f, 0.f, 1.f); glTexCoord2f(0.5f, 0.5f); glVertex3f(0.f, 0.f, r);
        for (int j = 0; j <= slices; j++) {
            float phi = 2.f * PI * j / slices;
            float cp = std::cos(phi), sp = std::sin(phi);
            float u, v; texUV(th, phi, u, v);
            glNormal3f(s*cp, s*sp, c); glTexCoord2f(u, v); glVertex3f(r*s*cp, r*s*sp, r*c);
        }
        glEnd();
    }
    // Remaining stacks: QUAD_STRIP.
    for (int i = 1; i < stacks; i++) {
        float th0 = capAngle * i / stacks,   th1 = capAngle * (i+1) / stacks;
        float s0 = std::sin(th0), c0 = std::cos(th0);
        float s1 = std::sin(th1), c1 = std::cos(th1);
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            float phi = 2.f * PI * j / slices;
            float cp = std::cos(phi), sp = std::sin(phi);
            float u0, v0, u1, v1;
            texUV(th0, phi, u0, v0); texUV(th1, phi, u1, v1);
            glNormal3f(s0*cp, s0*sp, c0); glTexCoord2f(u0, v0); glVertex3f(r*s0*cp, r*s0*sp, r*c0);
            glNormal3f(s1*cp, s1*sp, c1); glTexCoord2f(u1, v1); glVertex3f(r*s1*cp, r*s1*sp, r*c1);
        }
        glEnd();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// Draw a small spherical-cap dot at unit-direction (nx,ny,nz) on a ball of radius r.
inline void drawSphereDot(float r, float nx, float ny, float nz, float capAngle) {
    glPushMatrix();
    // Rotate +Z → (nx, ny, nz).  cross(Z, n) = (-ny, nx, 0), dot(Z, n) = nz.
    float ax = -ny, ay = nx;
    float sinA = std::sqrt(ax * ax + ay * ay);
    if (sinA > 1e-6f) {
        float angle = std::atan2(sinA, nz) * (180.f / PI);
        glRotatef(angle, ax / sinA, ay / sinA, 0.f);
    } else if (nz < 0.f) {
        glRotatef(180.f, 1.f, 0.f, 0.f);
    }
    drawSphericalCap(r, capAngle);
    glPopMatrix();
}


inline void applyBallTransform(const Ball& b) {
    glTranslatef(b.pos.x, b.pos.y, b.pos.z);
    float R[16] = {
        b.orient.m[0], b.orient.m[1], b.orient.m[2], 0,
        b.orient.m[3], b.orient.m[4], b.orient.m[5], 0,
        b.orient.m[6], b.orient.m[7], b.orient.m[8], 0,
        0, 0, 0, 1
    };
    glMultMatrixf(R);
}

inline void drawCueBall(const Ball& b) {
    glPushMatrix(); applyBallTransform(b);
    glColor3f(0.95f, 0.95f, 0.92f); drawSphere(b.radius);

    // 6 red dots at octahedron vertices: 3 polar-opposite pairs, all equidistant.
    glDisable(GL_LIGHTING);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-2, -2);
    glColor3f(0.82f, 0.07f, 0.07f);
    static const float kDirs[6][3] = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1}
    };
    constexpr float kDotAngle = 0.22f; // ~12.6° half-angle per dot
    for (auto& d : kDirs)
        drawSphereDot(b.radius, d[0], d[1], d[2], kDotAngle);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

inline void drawEightBall(const Ball& b) {
    glPushMatrix(); applyBallTransform(b);
    glColor3f(0.02f, 0.02f, 0.02f); drawSphere(b.radius);

    glDisable(GL_LIGHTING);

    float r        = b.radius;
    // Render caps at a hair above the sphere surface so their depth is always
    // unambiguously in front of the black ball — eliminates depth-fighting vs the
    // sphere from any camera angle, including face-on.  0.04" on a 1.125" ball
    // is sub-pixel at normal table distances and invisible to the eye.
    float rCap   = r + 0.04f;   // white circle layer
    float rGlyph = r + 0.06f;   // glyph layer, strictly above white
    float capAngle = 0.46f;

    for (int side = 0; side < 2; side++) {
        glPushMatrix();
        if (side) glRotatef(180.f, 0.f, 1.f, 0.f);

        glColor3f(1.f, 1.f, 1.f);
        drawSphericalCap(rCap, capAngle);

        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor3f(0.f, 0.f, 0.f);
        drawSphericalCapTextured(g_eightTex, rGlyph, capAngle);
        glDisable(GL_BLEND);

        glPopMatrix();
    }

    glEnable(GL_LIGHTING);
    glPopMatrix();
}

// ---------------------------------------------------------------------------
inline void drawCushionSeg(const CushionSeg& s) {
    float h = Table::CUSHION_HEIGHT, d = Table::CUSHION_DEPTH;
    Vec3 out = -s.inward;
    Vec3 a = s.a, b = s.b, ao = a + out * d, bo = b + out * d;

    glNormal3f(s.inward.x, 0, s.inward.z);
    glBegin(GL_QUADS);
    glVertex3f(a.x,0,a.z); glVertex3f(a.x,h,a.z);
    glVertex3f(b.x,h,b.z); glVertex3f(b.x,0,b.z); glEnd();
    glNormal3f(0,1,0);
    glBegin(GL_QUADS);
    glVertex3f(a.x,h,a.z);  glVertex3f(ao.x,h,ao.z);
    glVertex3f(bo.x,h,bo.z);glVertex3f(b.x,h,b.z); glEnd();
    glNormal3f(out.x,0,out.z);
    glBegin(GL_QUADS);
    glVertex3f(ao.x,0,ao.z);glVertex3f(bo.x,0,bo.z);
    glVertex3f(bo.x,h,bo.z);glVertex3f(ao.x,h,ao.z); glEnd();
    Vec3 dir = normalize(Vec3{b.x-a.x,0,b.z-a.z});
    glNormal3f(-dir.x,0,-dir.z);
    glBegin(GL_QUADS);
    glVertex3f(a.x,0,a.z);glVertex3f(ao.x,0,ao.z);
    glVertex3f(ao.x,h,ao.z);glVertex3f(a.x,h,a.z); glEnd();
    glNormal3f(dir.x,0,dir.z);
    glBegin(GL_QUADS);
    glVertex3f(b.x,0,b.z);glVertex3f(b.x,h,b.z);
    glVertex3f(bo.x,h,bo.z);glVertex3f(bo.x,0,bo.z); glEnd();
}

inline void drawTable(const std::vector<CushionSeg>& segs,
                      const std::vector<Pocket>& pockets) {
    float hw = Table::HALF_W, hl = Table::HALF_L;
    float rail = Table::RAIL_WIDTH, rh = Table::CUSHION_HEIGHT + 1.0f;
    float ext = 3.5f;

    glColor3f(0.0f, 0.45f, 0.25f);
    glNormal3f(0,1,0);
    glBegin(GL_QUADS);
    glVertex3f(-hw-ext,0,-hl-ext); glVertex3f(hw+ext,0,-hl-ext);
    glVertex3f(hw+ext,0,hl+ext);   glVertex3f(-hw-ext,0,hl+ext);
    glEnd();

    glDisable(GL_LIGHTING);
    glColor3f(0.02f,0.02f,0.02f);
    for (auto& p : pockets) {
        glPushMatrix();
        glTranslatef(p.center.x, 0.02f, p.center.z);
        glRotatef(-90,1,0,0);
        drawDisk(p.captureRadius + 0.4f, 32);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);

    glColor3f(0.0f,0.35f,0.18f);
    for (auto& s : segs) drawCushionSeg(s);

    glColor3f(0.40f,0.25f,0.10f);
    float ow=hw+rail, ol=hl+rail;
    glNormal3f(-1,0,0); glBegin(GL_QUADS);
    glVertex3f(-ow,0,-ol);glVertex3f(-ow,rh,-ol);glVertex3f(-ow,rh,ol);glVertex3f(-ow,0,ol);glEnd();
    glNormal3f(1,0,0); glBegin(GL_QUADS);
    glVertex3f(ow,0,-ol);glVertex3f(ow,0,ol);glVertex3f(ow,rh,ol);glVertex3f(ow,rh,-ol);glEnd();
    glNormal3f(0,0,-1); glBegin(GL_QUADS);
    glVertex3f(-ow,0,-ol);glVertex3f(ow,0,-ol);glVertex3f(ow,rh,-ol);glVertex3f(-ow,rh,-ol);glEnd();
    glNormal3f(0,0,1); glBegin(GL_QUADS);
    glVertex3f(-ow,0,ol);glVertex3f(-ow,rh,ol);glVertex3f(ow,rh,ol);glVertex3f(ow,0,ol);glEnd();

    glColor3f(0.45f,0.28f,0.12f);
    glNormal3f(0,1,0);
    float iw=hw+Table::CUSHION_DEPTH, il=hl+Table::CUSHION_DEPTH;
    glBegin(GL_QUADS);
    glVertex3f(-ow,rh,-ol);glVertex3f(-iw,rh,-il);glVertex3f(-iw,rh,il);glVertex3f(-ow,rh,ol);
    glVertex3f(iw,rh,-il);glVertex3f(ow,rh,-ol);glVertex3f(ow,rh,ol);glVertex3f(iw,rh,il);
    glVertex3f(-iw,rh,-il);glVertex3f(-ow,rh,-ol);glVertex3f(ow,rh,-ol);glVertex3f(iw,rh,-il);
    glVertex3f(-iw,rh,il);glVertex3f(iw,rh,il);glVertex3f(ow,rh,ol);glVertex3f(-ow,rh,ol);
    glEnd();
}

// ---------------------------------------------------------------------------
inline void drawAimArrow(const Ball& cue, Vec3 dir, float power01) {
    if (cue.pocketed) return;
    Vec3 base = cue.pos + dir * (cue.radius + 0.3f); base.y = cue.radius;
    Vec3 tip  = base + dir * 5.0f;
    Vec3 rgt  = cross(dir, Vec3{0,1,0});
    Vec3 h1 = tip - dir*1.2f + rgt*0.6f, h2 = tip - dir*1.2f - rgt*0.6f;

    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
    glLineWidth(3.0f);
    glColor3f(std::min(1.f,power01*2.f), std::min(1.f,2.f-power01*2.f), 0.1f);
    glBegin(GL_LINES);
    glVertex3f(base.x,base.y,base.z); glVertex3f(tip.x,tip.y,tip.z);
    glVertex3f(tip.x,tip.y,tip.z);    glVertex3f(h1.x,h1.y,h1.z);
    glVertex3f(tip.x,tip.y,tip.z);    glVertex3f(h2.x,h2.y,h2.z);
    glEnd();
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
}

// ---------------------------------------------------------------------------
inline void beginHUD(int w, int h) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
inline void endHUD() {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
}

inline void drawStrengthBar(float power01) {
    float bx=20, by=20, bw=240, bh=18;
    glColor4f(0.15f,0.15f,0.15f,0.85f);
    glBegin(GL_QUADS);
    glVertex2f(bx,by);glVertex2f(bx+bw,by);glVertex2f(bx+bw,by+bh);glVertex2f(bx,by+bh);glEnd();
    float fw=bw*power01;
    glColor4f(std::min(1.f,power01*2.f),std::min(1.f,2.f-power01*2.f),0.1f,0.95f);
    glBegin(GL_QUADS);
    glVertex2f(bx,by);glVertex2f(bx+fw,by);glVertex2f(bx+fw,by+bh);glVertex2f(bx,by+bh);glEnd();
    glColor4f(0.6f,0.6f,0.6f,0.9f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(bx,by);glVertex2f(bx+bw,by);glVertex2f(bx+bw,by+bh);glVertex2f(bx,by+bh);glEnd();
}

// 2D cue-ball diagram with tip-contact dot.
// (tipX, tipY) ∈ unit disk. +X = right english, +Y = topspin/follow.
inline void drawSpinDiagram(float tipX, float tipY, bool editing) {
    float R  = 55.f;
    float cx = 20.f + R;
    float cy = 20.f + 18.f + 16.f + R;   // just above strength bar

    // translucent ball face
    glColor4f(0.95f, 0.95f, 0.92f, 0.55f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i=0;i<=48;i++){float a=2*PI*i/48; glVertex2f(cx+R*std::cos(a),cy+R*std::sin(a));}
    glEnd();

    // crosshair
    glLineWidth(1.0f);
    glColor4f(0.25f,0.25f,0.25f,0.6f);
    glBegin(GL_LINES);
    glVertex2f(cx-R,cy);glVertex2f(cx+R,cy);
    glVertex2f(cx,cy-R);glVertex2f(cx,cy+R);
    glEnd();

    // outline — highlighted while editing
    glLineWidth(editing ? 3.f : 1.5f);
    if (editing) glColor4f(1.0f, 0.85f, 0.2f, 0.95f);
    else         glColor4f(0.7f, 0.7f, 0.7f, 0.9f);
    glBegin(GL_LINE_LOOP);
    for (int i=0;i<48;i++){float a=2*PI*i/48; glVertex2f(cx+R*std::cos(a),cy+R*std::sin(a));}
    glEnd();

    // red tip-contact dot
    float px = cx + tipX*R, py = cy + tipY*R, dr = 6.f;
    glColor4f(0.90f, 0.10f, 0.10f, 0.95f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(px,py);
    for (int i=0;i<=20;i++){float a=2*PI*i/20; glVertex2f(px+dr*std::cos(a),py+dr*std::sin(a));}
    glEnd();
    glColor4f(0.3f,0,0,1);
    glLineWidth(1.f);
    glBegin(GL_LINE_LOOP);
    for (int i=0;i<20;i++){float a=2*PI*i/20; glVertex2f(px+dr*std::cos(a),py+dr*std::sin(a));}
    glEnd();
}

// ---------------------------------------------------------------------------
inline void perspective(float fovY, float aspect, float zn, float zf) {
    float f=1.f/std::tan(fovY*0.5f);
    float M[16]={f/aspect,0,0,0, 0,f,0,0, 0,0,(zf+zn)/(zn-zf),-1, 0,0,(2*zf*zn)/(zn-zf),0};
    glMultMatrixf(M);
}
inline void lookAt(Vec3 eye, Vec3 tgt, Vec3 up) {
    Vec3 f=normalize(tgt-eye), s=normalize(cross(f,up)), u=cross(s,f);
    float M[16]={s.x,u.x,-f.x,0, s.y,u.y,-f.y,0, s.z,u.z,-f.z,0, 0,0,0,1};
    glMultMatrixf(M); glTranslatef(-eye.x,-eye.y,-eye.z);
}

} // namespace Render
#endif