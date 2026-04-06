#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <cstdio>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>

#include "vec3.hpp"
#include "physics.hpp"
#include "table.hpp"
#include "render.hpp"

// ============================================================================
struct Game {
    PhysicsConfig        config;
    std::vector<Ball>    balls;     // balls[0]=cue, balls[1-15]=numbered balls
    std::vector<CushionSeg> cushions;
    std::vector<Pocket>  pockets;

    bool   inMotion      = false;
    int    shotStartStep = 0;
    double physicsTime   = 0;
    int    physicsSteps  = 0;

    std::mt19937 rng{std::random_device{}()};

    void buildTable() {
        cushions = Table::getCushionSegs();
        pockets  = Table::getPockets();
    }

    void reset() {
        balls.assign(16, Ball{});

        // --- Cue ball ---
        balls[0].pos    = Table::cueBallStart();
        balls[0].radius = Table::BALL_RADIUS;
        balls[0].number = 0;
        balls[0].type   = Ball::CUE;

        // --- Rack positions: 15 slots, row-major close-packed triangle ---
        // Index 4  = centre of row 2 → always the 8-ball.
        // Index 10 = left back corner of row 4 → one solid, one stripe (randomised).
        // Index 14 = right back corner of row 4.
        std::vector<Vec3> rackPos = Table::getRackPositions();

        std::vector<int> solids  = {1, 2, 3, 4, 5, 6, 7};
        std::vector<int> stripes = {9, 10, 11, 12, 13, 14, 15};
        std::shuffle(solids.begin(),  solids.end(),  rng);
        std::shuffle(stripes.begin(), stripes.end(), rng);

        // Assign one corner a solid, the other a stripe; randomise which.
        int corner10, corner14;
        if (rng() & 1) {
            corner10 = solids.back();  solids.pop_back();
            corner14 = stripes.back(); stripes.pop_back();
        } else {
            corner10 = stripes.back(); stripes.pop_back();
            corner14 = solids.back();  solids.pop_back();
        }

        // Remaining 12 balls shuffled into the other 12 positions.
        std::vector<int> rest;
        rest.insert(rest.end(), solids.begin(),  solids.end());
        rest.insert(rest.end(), stripes.begin(), stripes.end());
        std::shuffle(rest.begin(), rest.end(), rng);

        int ri = 0;
        for (int pos = 0; pos < 15; pos++) {
            int num;
            if      (pos == 4)  num = 8;
            else if (pos == 10) num = corner10;
            else if (pos == 14) num = corner14;
            else                num = rest[ri++];

            Ball& b  = balls[num];
            b.pos    = rackPos[pos];
            b.radius = Table::BALL_RADIUS;
            b.number = num;
            b.type   = (num == 8)              ? Ball::EIGHT
                      : (num >= 1 && num <= 7) ? Ball::SOLID
                                               : Ball::STRIPE;
        }

        inMotion = false; physicsTime = 0; physicsSteps = 0;
        printf("[RESET] Balls racked (8 at centre, corners randomised).\n");
    }

    // dir: unit aim direction (XZ), mph: shot speed
    // tipX/Y: normalised tip offset on the cue-ball face, |(x,y)| ≤ 1
    void shoot(Vec3 dir, float mph, float tipX, float tipY) {
        float v = mph * MPH_TO_INS;
        balls[0].vel = dir * v;

        float R = balls[0].radius;
        float spinScale  = 1.25f * v / R;
        Vec3  followAxis = cross(Vec3{0, 1, 0}, dir);

        balls[0].angVel = followAxis * (tipY * spinScale)
                        + Vec3{0, 1, 0} * (tipX * spinScale);

        inMotion      = true;
        shotStartStep = physicsSteps;

        const char* vs = (tipY >  0.08f) ? "FOLLOW" : (tipY < -0.08f) ? "DRAW" : "STUN";
        const char* hs = (tipX >  0.08f) ? "RIGHT"  : (tipX < -0.08f) ? "LEFT" : "";
        printf("[SHOT] %.2f mph  tip=(%+.2f, %+.2f)  [%s%s%s]  ω=(%.1f,%.1f,%.1f) rad/s\n",
               mph, tipX, tipY, vs, (*hs ? " + " : ""), hs,
               balls[0].angVel.x, balls[0].angVel.y, balls[0].angVel.z);
    }

    void step() {
        auto t0 = std::chrono::high_resolution_clock::now();

        for (auto& b : balls) integrate(b, config);

        for (auto& b : balls)
            for (auto& c : cushions)
                collideCushion(b, c, config.railRestitution, config.railFriction);

        for (auto& b : balls)
            for (auto& p : pockets)
                if (checkPocket(b, p))
                    printf("[POCKET] Ball %d pocketed!\n", b.number);

        // O(n²) ball-ball collisions (16 balls → 120 pairs at 480 Hz)
        for (int i = 0; i < (int)balls.size(); i++)
            for (int j = i + 1; j < (int)balls.size(); j++)
                collideBalls(balls[i], balls[j], config);

        auto t1 = std::chrono::high_resolution_clock::now();
        physicsTime += std::chrono::duration<double, std::milli>(t1 - t0).count();
        physicsSteps++;
    }

    bool ballsAtRest() const {
        for (const auto& b : balls) {
            if (b.frozen) continue;
            if (b.pocketed) { if (b.pos.y > -5.9f) return false; continue; }
            if (length(b.vel) >= 0.05f || length(b.angVel) >= 0.05f) return false;
        }
        return true;
    }

    float elapsedSimTime() const { return (physicsSteps - shotStartStep) * config.dt; }
};

// ============================================================================
struct Camera {
    float yaw = 3.14159f, pitch = 0.45f, dist = 115.0f;
    Vec3  target{0, 0, 10};   // aimed toward foot-spot / rack area
    Vec3 aimDir()   const { return normalize(Vec3{-std::sin(yaw), 0, -std::cos(yaw)}); }
    Vec3 rightDir() const { return cross(aimDir(), Vec3{0, 1, 0}); }
};

static Camera cam;
static double lastMouseX, lastMouseY;
static bool   dragging = false;

void cursorCallback(GLFWwindow*, double x, double y) {
    if (dragging) {
        // Keep the eye position fixed; pivot the look direction in place.
        Vec3 fwd{
            std::cos(cam.pitch) * std::sin(cam.yaw),
            std::sin(cam.pitch),
            std::cos(cam.pitch) * std::cos(cam.yaw)
        };
        Vec3 eye = cam.target + fwd * cam.dist;

        cam.yaw   += (float)(x - lastMouseX) * 0.005f;
        cam.pitch += (float)(y - lastMouseY) * 0.005f;
        cam.pitch  = std::clamp(cam.pitch, 0.05f, 1.5f);

        Vec3 newFwd{
            std::cos(cam.pitch) * std::sin(cam.yaw),
            std::sin(cam.pitch),
            std::cos(cam.pitch) * std::cos(cam.yaw)
        };
        cam.target = eye - newFwd * cam.dist;
    }
    lastMouseX = x; lastMouseY = y;
}
void mouseCallback(GLFWwindow*, int b, int a, int) {
    if (b == GLFW_MOUSE_BUTTON_LEFT) dragging = (a == GLFW_PRESS);
}
void scrollCallback(GLFWwindow*, double, double dy) {
    cam.dist *= std::pow(0.9f, (float)dy);
    cam.dist  = std::clamp(cam.dist, 20.0f, 300.0f);
}

// ============================================================================
int main() {
    if (!glfwInit()) { fprintf(stderr, "GLFW init failed\n"); return 1; }
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Pool Table Physics", nullptr, nullptr);
    if (!window) { fprintf(stderr, "Window failed\n"); glfwTerminate(); return 1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    float lightPos[] = {0, 100, 0, 1};
    float lightAmb[] = {0.3f, 0.3f, 0.3f, 1};
    float lightDif[] = {0.9f, 0.9f, 0.85f, 1};
    glLightfv(GL_LIGHT0, GL_AMBIENT,  lightAmb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  lightDif);

    Render::init();

    Game game; game.buildTable(); game.reset();

    using Clock = std::chrono::high_resolution_clock;
    auto lastTime = Clock::now(); double accumulator = 0;

    bool  levitating   = false;
    bool  spinEdit     = false;
    float shotPowerMph = 17.0f;
    const float MIN_MPH = 0.1f, MAX_MPH = 17.0f;
    float tipX = 0.f, tipY = 0.f;

    bool enterWas = false, eWas = false, rWas = false, qWas = false;

    while (!glfwWindowShouldClose(window)) {
        auto   now     = Clock::now();
        double frameDt = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now; accumulator += frameDt;

        bool atRest  = !game.inMotion;
        Vec3 aimDir  = cam.aimDir();
        Vec3 rgtDir  = cam.rightDir();

        // --- R reset ---
        bool rDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        if (rDown && !rWas) { game.reset(); levitating = false; spinEdit = false; }
        rWas = rDown;
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        // --- power ---
        float pr = 6.0f * (float)frameDt;
        if (glfwGetKey(window, GLFW_KEY_UP)   == GLFW_PRESS) shotPowerMph += pr;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) shotPowerMph -= pr;
        shotPowerMph = std::clamp(shotPowerMph, MIN_MPH, MAX_MPH);

        // --- Q: toggle spin editor ---
        bool qDown = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
        if (qDown && !qWas && atRest && !levitating) {
            spinEdit = !spinEdit;
            if (spinEdit) printf("[SPIN] Editor ON. WASD moves tip contact. Q to confirm.\n");
            else          printf("[SPIN] Tip offset = (%+.2f, %+.2f)\n", tipX, tipY);
        }
        qWas = qDown;

        // --- E: ball-in-hand ---
        bool eDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        if (eDown && !eWas && atRest && !spinEdit) {
            Ball& cue = game.balls[0];
            if (!levitating) {
                if (cue.pocketed) { cue.pocketed = false; cue.pos = {0, 0, 0}; }
                levitating   = true;
                cue.frozen   = true;
                cue.vel      = {};
                cue.angVel   = {};
                cue.pos.y    = Table::BALL_RADIUS + 2.0f;
                printf("[PLACE] Ball-in-hand ON.\n");
            } else {
                levitating = false;
                cue.frozen = false;
                cue.pos.y  = Table::BALL_RADIUS;
                cue.vel    = {};
                cue.angVel = {};
                printf("[PLACE] Cue placed at (%.2f, %.2f).\n", cue.pos.x, cue.pos.z);
            }
        }
        eWas = eDown;

        // --- WASD routing ---
        bool wK = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
        bool sK = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
        bool aK = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
        bool dK = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;

        if (spinEdit) {
            float rate = 1.4f * (float)frameDt;
            if (wK) tipY += rate;  if (sK) tipY -= rate;
            if (dK) tipX += rate;  if (aK) tipX -= rate;
            float m = std::sqrt(tipX * tipX + tipY * tipY);
            if (m > 1.f) { tipX /= m; tipY /= m; }
        } else if (levitating) {
            Ball& cue = game.balls[0];
            float mv = 15.0f * (float)frameDt; Vec3 dp{};
            if (wK) dp += aimDir; if (sK) dp -= aimDir;
            if (dK) dp += rgtDir; if (aK) dp -= rgtDir;
            cue.pos += dp * mv;
            float r = cue.radius, pad = 0.2f;
            cue.pos.x = std::clamp(cue.pos.x, -Table::HALF_W + r + pad, Table::HALF_W - r - pad);
            cue.pos.z = std::clamp(cue.pos.z, -Table::HALF_L + r + pad, Table::HALF_L - r - pad);
            cue.pos.y = Table::BALL_RADIUS + 2.0f;
            // Push away from any non-cue ball
            for (int i = 1; i < (int)game.balls.size(); i++) {
                Ball& ob = game.balls[i];
                if (ob.pocketed) continue;
                Vec3 d{cue.pos.x - ob.pos.x, 0, cue.pos.z - ob.pos.z};
                float dist = length(d), minD = 2 * r + 0.1f;
                if (dist < minD && dist > 1e-4f) cue.pos += normalize(d) * (minD - dist);
            }
        } else {
            float mv = 30.0f * (float)frameDt;
            if (wK) cam.target += aimDir * mv; if (sK) cam.target -= aimDir * mv;
            if (dK) cam.target += rgtDir * mv; if (aK) cam.target -= rgtDir * mv;
            bool spaceK = glfwGetKey(window, GLFW_KEY_SPACE)      == GLFW_PRESS;
            bool shiftK = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            if (spaceK) cam.target.y += mv;
            if (shiftK) cam.target.y -= mv;
        }

        // --- Enter: shoot ---
        bool enterDown = glfwGetKey(window, GLFW_KEY_ENTER)    == GLFW_PRESS
                      || glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS;
        if (enterDown && !enterWas && atRest && !levitating && !spinEdit
                      && !game.balls[0].pocketed)
            game.shoot(aimDir, shotPowerMph, tipX, tipY);
        enterWas = enterDown;

        // --- physics ---
        while (accumulator >= game.config.dt) { game.step(); accumulator -= game.config.dt; }

        if (game.inMotion && game.ballsAtRest()) {
            game.inMotion = false;
            printf("[SETTLED] Time elapsed: %.2f s  (%d steps, avg %.4f ms/step)\n",
                   game.elapsedSimTime(),
                   game.physicsSteps - game.shotStartStep,
                   game.physicsTime / game.physicsSteps);
        }

        // --- render ---
        int w, h; glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.12f, 0.12f, 0.15f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        Render::perspective(50.0f * Render::PI / 180.0f, (float)w / h, 1.0f, 500.0f);
        glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
        Vec3 eye = cam.target + Vec3(
            std::cos(cam.pitch) * std::sin(cam.yaw),
            std::sin(cam.pitch),
            std::cos(cam.pitch) * std::cos(cam.yaw)) * cam.dist;
        Render::lookAt(eye, cam.target, {0, 1, 0});
        glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

        Render::drawTable(game.cushions, game.pockets);
        for (auto& b : game.balls) Render::drawBall(b);

        float power01 = (shotPowerMph - MIN_MPH) / (MAX_MPH - MIN_MPH);
        if (atRest && !game.balls[0].pocketed)
            Render::drawAimArrow(game.balls[0], aimDir, power01);

        Render::beginHUD(w, h);
        Render::drawStrengthBar(power01);
        Render::drawSpinDiagram(tipX, tipY, spinEdit);
        Render::drawControls(w, h);
        Render::endHUD();

        char title[160];
        const char* mode = spinEdit    ? "[SPIN EDIT] "
                         : levitating ? "[PLACING] "
                         : atRest     ? "[READY] " : "[IN MOTION] ";
        snprintf(title, sizeof(title),
                 "Pool  |  Power %.2f mph  |  Tip (%+.2f, %+.2f)  %s%s",
                 shotPowerMph, tipX, tipY, mode,
                 game.balls[0].pocketed ? "(cue pocketed — press E) " : "");
        glfwSetWindowTitle(window, title);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    Render::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
