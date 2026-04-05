#ifndef TABLE_HPP
#define TABLE_HPP

#include "physics.hpp"
#include <vector>
#include <cmath>

namespace Table {
    // 9-foot table, all inches
    constexpr float WIDTH   = 50.0f;
    constexpr float LENGTH  = 100.0f;
    constexpr float HALF_W  = WIDTH  / 2.0f;
    constexpr float HALF_L  = LENGTH / 2.0f;

    constexpr float BALL_DIAMETER = 2.25f;
    constexpr float BALL_RADIUS   = BALL_DIAMETER / 2.0f;

    constexpr float CUSHION_HEIGHT  = 1.43f;
    constexpr float CUSHION_DEPTH   = 1.8f;   // visual thickness outward
    constexpr float RAIL_WIDTH      = 5.0f;

    constexpr float CORNER_POCKET_W = 4.75f;
    constexpr float SIDE_POCKET_W   = 5.25f;

    // Derived cutbacks
    constexpr float CORNER_CUT = CORNER_POCKET_W * 0.70710678f;  // mouth / √2 ≈ 3.36"
    constexpr float SIDE_CUT   = SIDE_POCKET_W / 2.0f;           // ≈ 2.625"
    constexpr float JAW_LEN    = 2.5f;
    constexpr float SIDE_JAW_ANGLE = 12.0f * 3.14159265f / 180.0f; // from perpendicular

    inline Vec3 cueBallStart() { return {0, BALL_RADIUS, -HALF_L / 2.0f}; }

    // 15 rack positions in a close-packed triangle.
    // Index 4  = centre of row 2 → always the 8-ball.
    // Indices 10 & 14 = back corners of row 4 (one solid, one stripe).
    // Apex (index 0) sits on the foot spot at z = +HALF_L/2.
    inline std::vector<Vec3> getRackPositions() {
        std::vector<Vec3> pos;
        pos.reserve(15);
        const float D    = BALL_DIAMETER;
        const float rowH = D * 0.8660254f;      // D * sqrt(3)/2
        const float z0   = HALF_L / 2.0f;       // foot spot, +25 inches from centre
        for (int row = 0; row < 5; row++) {
            float z       = z0 + row * rowH;
            float x_start = -(row * D / 2.0f);
            for (int col = 0; col <= row; col++)
                pos.push_back({x_start + col * D, BALL_RADIUS, z});
        }
        return pos;
    }

    // -----------------------------------------------------------------------
    // Build cushion segments: 6 main rails + 8 corner jaws + 4 side jaws = 18
    // `inward` computed as the perpendicular pointing toward refPoint.
    // -----------------------------------------------------------------------
    inline std::vector<CushionSeg> getCushionSegs() {
        std::vector<CushionSeg> segs;
        auto add = [&](Vec3 a, Vec3 b, Vec3 ref) {
            Vec3 dir  = normalize(Vec3{b.x - a.x, 0, b.z - a.z});
            Vec3 perp = cross(Vec3{0, 1, 0}, dir);
            Vec3 mid  = (a + b) * 0.5f;
            if (dot(perp, ref - mid) < 0) perp = -perp;
            segs.push_back({a, b, perp});
        };

        const float hw = HALF_W, hl = HALF_L, cc = CORNER_CUT, sc = SIDE_CUT;
        const float s2 = 0.70710678f, jl = JAW_LEN;
        const Vec3 O{0, 0, 0};

        // --- Main rails (6) --- refPoint = origin
        add({-hw + cc, 0, -hl}, { hw - cc, 0, -hl}, O);                 // foot rail
        add({-hw + cc, 0,  hl}, { hw - cc, 0,  hl}, O);                 // head rail
        add({-hw, 0, -hl + cc}, {-hw, 0, -sc},      O);                 // left lower
        add({-hw, 0,  sc},      {-hw, 0,  hl - cc}, O);                 // left upper
        add({ hw, 0, -hl + cc}, { hw, 0, -sc},      O);                 // right lower
        add({ hw, 0,  sc},      { hw, 0,  hl - cc}, O);                 // right upper

        // --- Corner jaws (8) --- refPoint = corner itself
        for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            Vec3 corner{sx * hw, 0, sz * hl};
            Vec3 tipA{sx * (hw - cc), 0, sz * hl};         // on short rail
            Vec3 tipB{sx * hw,        0, sz * (hl - cc)};  // on long rail
            Vec3 jdir{sx * -s2, 0, sz * -s2};              // toward/outside corner? -> outward
            jdir = Vec3{sx * s2, 0, sz * s2};              // points OUTWARD past corner
            add(tipA, tipA + jdir * jl, corner);
            add(tipB, tipB + jdir * jl, corner);
        }

        // --- Side jaws (4) --- refPoint = rail point at pocket
        float jx = std::cos(SIDE_JAW_ANGLE) * jl;  // outward
        float jz = std::sin(SIDE_JAW_ANGLE) * jl;  // toward pocket center
        for (int sx = -1; sx <= 1; sx += 2) {
            Vec3 ref{sx * hw, 0, 0};
            Vec3 tipLo{sx * hw, 0, -sc};
            Vec3 tipHi{sx * hw, 0,  sc};
            add(tipLo, tipLo + Vec3{sx * jx, 0,  jz}, ref);
            add(tipHi, tipHi + Vec3{sx * jx, 0, -jz}, ref);
        }

        return segs;
    }

    // -----------------------------------------------------------------------
    inline std::vector<Pocket> getPockets() {
        std::vector<Pocket> p;
        for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            p.push_back({ {sx * (HALF_W + 1.5f), 0, sz * (HALF_L + 1.5f)}, 2.8f }); // corners
        for (int sx = -1; sx <= 1; sx += 2)
            p.push_back({ {sx * (HALF_W + 2.5f), 0, 0}, 2.3f });                    // sides
        return p;
    }
}

#endif