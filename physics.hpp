#ifndef PHYSICS_HPP
#define PHYSICS_HPP

#include "vec3.hpp"
#include <algorithm>

// Units: INCHES, SECONDS
constexpr float GRAVITY_IN_S2 = 386.09f;
constexpr float MPH_TO_INS    = 17.6f;

struct PhysicsConfig {
    Vec3  gravity{0, -GRAVITY_IN_S2, 0};
    float ballBallRestitution = 0.98f;
    float ballBallFriction    = 0.05f;
    float clothSliding        = 0.20f;
    float clothRolling        = 0.010f;
    float spinDecel           = 10.0f;
    float railRestitution     = 0.75f;
    float railFriction        = 0.10f;
    float tableRestitution    = 0.50f;
    float dt                  = 1.f / 480.f;
};

struct Ball {
    Vec3  pos{}, vel{}, angVel{};
    Mat3  orient{};
    Vec3  color{1, 1, 1};
    float radius = 1.125f;
    float mass   = 6.0f;
    bool  pocketed = false;
    bool  frozen   = false;          // skip physics (ball-in-hand placement)
    enum Type { CUE, EIGHT, SOLID, STRIPE } type = SOLID;
    float inertia() const { return 0.4f * mass * radius * radius; }
};

// Finite cushion: a line segment in the XZ plane. Collision normal is
// computed from the closest point, so endpoints behave as rounded "points".
struct CushionSeg {
    Vec3 a, b;       // endpoints (y = 0)
    Vec3 inward;     // unit normal toward play area (rendering only)
};

struct Pocket {
    Vec3  center;        // throat center (y = 0)
    float captureRadius;
};

// ---------------------------------------------------------------------------
inline void integrate(Ball& b, const PhysicsConfig& cfg) {
    if (b.frozen) return;

    if (b.pocketed) {
        b.vel += cfg.gravity * cfg.dt;
        b.pos += b.vel * cfg.dt;
        if (b.pos.y < -6.0f) { b.pos.y = -6.0f; b.vel = {}; b.angVel = {}; }
        return;
    }

    b.vel += cfg.gravity * cfg.dt;
    b.pos += b.vel * cfg.dt;

    bool onTable = false;
    if (b.pos.y <= b.radius) {
        b.pos.y = b.radius;
        if (b.vel.y < 0)
            b.vel.y = (b.vel.y < -20.0f) ? -cfg.tableRestitution * b.vel.y : 0.0f;
        onTable = true;
    }

    if (onTable) {
        Vec3 rC{0, -b.radius, 0};
        Vec3 vC = b.vel + cross(b.angVel, rC);
        Vec3 vSlip{vC.x, 0, vC.z};
        float slip = length(vSlip);

        if (slip > 0.5f) {                               // SLIDING
            Vec3 dir = vSlip * (1.0f / slip);
            float fMag = cfg.clothSliding * b.mass * GRAVITY_IN_S2;
            Vec3 F = -dir * fMag;
            b.vel    += F * (1.0f / b.mass)      * cfg.dt;
            b.angVel += cross(rC, F) * (1.0f / b.inertia()) * cfg.dt;
        } else {                                          // ROLLING
            float speed = length(Vec3{b.vel.x, 0, b.vel.z});
            if (speed > 0.05f) {
                Vec3 dir = Vec3{b.vel.x, 0, b.vel.z} * (1.0f / speed);
                float dv = std::min(cfg.clothRolling * GRAVITY_IN_S2 * cfg.dt, speed);
                b.vel -= dir * dv;
                b.angVel.x =  b.vel.z / b.radius;
                b.angVel.z = -b.vel.x / b.radius;
            } else {
                b.vel.x = b.vel.z = 0;
                b.angVel.x = b.angVel.z = 0;
            }
        }

        if (std::fabs(b.angVel.y) > 0.01f) {
            float d = cfg.spinDecel * cfg.dt;
            b.angVel.y = (b.angVel.y > 0) ? std::max(0.0f, b.angVel.y - d)
                                          : std::min(0.0f, b.angVel.y + d);
        }
    }

    Mat3 W{};
    W.m[0]=0;           W.m[3]=-b.angVel.z; W.m[6]= b.angVel.y;
    W.m[1]= b.angVel.z; W.m[4]=0;           W.m[7]=-b.angVel.x;
    W.m[2]=-b.angVel.y; W.m[5]= b.angVel.x; W.m[8]=0;
    Mat3 dR = W * b.orient;
    for (int i = 0; i < 9; i++) b.orient.m[i] += dR.m[i] * cfg.dt;
    orthonormalize(b.orient);
}

// ---------------------------------------------------------------------------
// Ball ↔ finite cushion segment (handles flat face AND rounded tips).
inline bool collideCushion(Ball& b, const CushionSeg& s, float e, float mu) {
    if (b.frozen || b.pocketed) return false;

    Vec3 A{s.a.x, 0, s.a.z}, B{s.b.x, 0, s.b.z}, P{b.pos.x, 0, b.pos.z};
    Vec3 AB = B - A;
    float ab2 = dot(AB, AB);
    float t = (ab2 > 1e-8f) ? std::clamp(dot(P - A, AB) / ab2, 0.0f, 1.0f) : 0.0f;
    Vec3 C = A + AB * t;

    Vec3 d = P - C;
    float dist = length(d);
    if (dist >= b.radius || dist < 1e-6f) return false;

    Vec3 n = d * (1.0f / dist);          // horizontal normal, cushion -> ball
    b.pos += n * (b.radius - dist);

    Vec3 rC = -n * b.radius;
    Vec3 vC = b.vel + cross(b.angVel, rC);
    float vn = dot(vC, n);
    if (vn >= 0) return true;

    float jn = -(1.f + e) * vn * b.mass;

    Vec3 vt = vC - n * vn;
    float vtLen = length(vt);
    Vec3 Jt{};
    if (vtLen > 1e-5f) {
        Vec3 tdir = vt * (1.f / vtLen);
        float I = b.inertia();
        float denom = 1.f / b.mass + (b.radius * b.radius) / I;
        float jtStick = -vtLen / denom;
        float jtMax = mu * jn;
        float jt = (std::fabs(jtStick) <= jtMax) ? jtStick : -mu * jn;
        Jt = tdir * jt;
    }

    Vec3 J = n * jn + Jt;
    b.vel    += J * (1.f / b.mass);
    b.angVel += cross(rC, J) * (1.f / b.inertia());
    return true;
}

// ---------------------------------------------------------------------------
// Ball ↔ Ball. Returns true on contact.
inline bool collideBalls(Ball& a, Ball& b, const PhysicsConfig& cfg) {
    if (a.frozen || b.frozen || a.pocketed || b.pocketed) return false;

    Vec3 delta = b.pos - a.pos;
    float dist = length(delta);
    float minDist = a.radius + b.radius;
    if (dist >= minDist || dist < 1e-6f) return false;

    Vec3 n = delta * (1.f / dist);
    float pen = minDist - dist;
    float invMa = 1.f / a.mass, invMb = 1.f / b.mass, invSum = invMa + invMb;

    a.pos -= n * (pen * invMa / invSum);
    b.pos += n * (pen * invMb / invSum);

    Vec3 rA =  n * a.radius;
    Vec3 rB = -n * b.radius;
    Vec3 vA = a.vel + cross(a.angVel, rA);
    Vec3 vB = b.vel + cross(b.angVel, rB);
    Vec3 vRel = vA - vB;
    float vn = dot(vRel, n);
    if (vn <= 0) return true;

    float jn = (1.f + cfg.ballBallRestitution) * vn / invSum;

    Vec3 vt = vRel - n * vn;
    float vtLen = length(vt);
    float jf = 0; Vec3 tdir{};
    if (vtLen > 1e-6f) {
        tdir = vt * (1.f / vtLen);
        float Ia = a.inertia(), Ib = b.inertia();
        float denom = invSum + (a.radius*a.radius)/Ia + (b.radius*b.radius)/Ib;
        float jfStick = vtLen / denom;
        float jfMax = cfg.ballBallFriction * jn;
        jf = (jfStick <= jfMax) ? jfStick : cfg.ballBallFriction * jn;
    }

    Vec3 J = n * (-jn) + tdir * (-jf);
    a.vel    += J * invMa;
    b.vel    -= J * invMb;
    a.angVel += cross(rA, J) * (1.f / a.inertia());
    b.angVel -= cross(rB, J) * (1.f / b.inertia());
    return true;
}

// ---------------------------------------------------------------------------
inline bool checkPocket(Ball& b, const Pocket& p) {
    if (b.frozen || b.pocketed) return false;
    Vec3 d{b.pos.x - p.center.x, 0, b.pos.z - p.center.z};
    if (length(d) < p.captureRadius) {
        b.pocketed = true;
        b.vel.x *= 0.3f; b.vel.z *= 0.3f;   // kill most horizontal speed, let it drop
        return true;
    }
    return false;
}

#endif