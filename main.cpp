#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <cstdio>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>

#include "vec3.hpp"
#include "physics.hpp"
#include "table.hpp"
#include "render.hpp"

// ============================================================================
struct Game {
    PhysicsConfig config;
    Ball cueBall, eightBall;
    std::vector<CushionSeg> cushions;
    std::vector<Pocket>     pockets;

    bool   inMotion      = false;
    int    shotStartStep = 0;
    double physicsTime   = 0;
    int    physicsSteps  = 0;

    void buildTable() {
        cushions = Table::getCushionSegs();
        pockets  = Table::getPockets();
    }

    void reset() {
        cueBall = Ball{};
        cueBall.pos    = Table::cueBallStart();
        cueBall.radius = Table::BALL_RADIUS;
        cueBall.type   = Ball::CUE;

        eightBall = Ball{};
        eightBall.pos    = Table::eightBallStart();
        eightBall.radius = Table::BALL_RADIUS;
        eightBall.type   = Ball::EIGHT;

        inMotion = false; physicsTime = 0; physicsSteps = 0;
        printf("[RESET] Balls at rest.\n");
    }

    // dir    : unit aim direction (XZ)
    // mph    : shot speed
    // tipX/Y : normalised tip offset on the cue-ball face, |(x,y)| ≤ 1.
    //          +Y = hit above centre (topspin/follow)
    //          −Y = hit below centre (backspin/draw)
    //          +X = hit shooter's right (right english)
    //          −X = hit shooter's left  (left english)
    void shoot(Vec3 dir, float mph, float tipX, float tipY) {
        float v = mph * MPH_TO_INS;
        cueBall.vel = dir * v;

        // ω from off-centre cue impact:  ω = (5/2)·(b/R²)·v  per offset axis.
        // Max usable offset ≈ 0.5·R (miscue limit) -> ω_max = 1.25·v/R.
        float R = cueBall.radius;
        float spinScale = 1.25f * v / R;               // at |tip| = 1
        Vec3  followAxis = cross(Vec3{0,1,0}, dir);    // horizontal ⟂ dir

        cueBall.angVel = followAxis * (tipY * spinScale)   // follow / draw
                       + Vec3{0,1,0} * (tipX * spinScale);  // side (english)

        inMotion      = true;
        shotStartStep = physicsSteps;

        const char* vs = (tipY >  0.08f) ? "FOLLOW" : (tipY < -0.08f) ? "DRAW"  : "STUN";
        const char* hs = (tipX >  0.08f) ? "RIGHT"  : (tipX < -0.08f) ? "LEFT"  : "";
        printf("[SHOT] %.2f mph  tip=(%+.2f, %+.2f)  [%s%s%s]  ω=(%.1f, %.1f, %.1f) rad/s\n",
               mph, tipX, tipY, vs, (*hs?" + ":""), hs,
               cueBall.angVel.x, cueBall.angVel.y, cueBall.angVel.z);
    }

    void step() {
        auto t0 = std::chrono::high_resolution_clock::now();

        integrate(cueBall,   config);
        integrate(eightBall, config);

        for (auto& c : cushions) {
            collideCushion(cueBall,   c, config.railRestitution, config.railFriction);
            collideCushion(eightBall, c, config.railRestitution, config.railFriction);
        }
        for (auto& p : pockets) {
            if (checkPocket(cueBall,   p)) printf("[POCKET] Cue ball pocketed!\n");
            if (checkPocket(eightBall, p)) printf("[POCKET] 8-ball pocketed!\n");
        }
        collideBalls(cueBall, eightBall, config);

        auto t1 = std::chrono::high_resolution_clock::now();
        physicsTime += std::chrono::duration<double, std::milli>(t1 - t0).count();
        physicsSteps++;
    }

    bool ballsAtRest() const {
        auto still = [](const Ball& b) {
            if (b.frozen) return true;
            if (b.pocketed) return b.pos.y <= -5.9f;
            return length(b.vel) < 0.05f && length(b.angVel) < 0.05f;
        };
        return still(cueBall) && still(eightBall);
    }
    float elapsedSimTime() const { return (physicsSteps - shotStartStep) * config.dt; }
};

// ============================================================================
struct Camera {
    float yaw = 3.14159f, pitch = 0.4f, dist = 80.0f;
    Vec3  target{0, 0, -15};
    Vec3 aimDir()   const { return normalize(Vec3{-std::sin(yaw), 0, -std::cos(yaw)}); }
    Vec3 rightDir() const { return cross(aimDir(), Vec3{0,1,0}); }
};

static Camera cam;
static double lastMouseX, lastMouseY;
static bool dragging = false;

void cursorCallback(GLFWwindow*, double x, double y) {
    if (dragging) {
        cam.yaw   += (float)(x-lastMouseX)*0.005f;
        cam.pitch += (float)(y-lastMouseY)*0.005f;
        cam.pitch  = std::clamp(cam.pitch, 0.1f, 1.5f);
    }
    lastMouseX=x; lastMouseY=y;
}
void mouseCallback(GLFWwindow*, int b, int a, int){ if(b==GLFW_MOUSE_BUTTON_LEFT) dragging=(a==GLFW_PRESS); }
void scrollCallback(GLFWwindow*, double, double dy){
    cam.dist *= std::pow(0.9f,(float)dy);
    cam.dist  = std::clamp(cam.dist, 20.0f, 250.0f);
}

// ============================================================================
int main() {
    if (!glfwInit()){ fprintf(stderr,"GLFW init failed\n"); return 1; }
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Pool Table Physics", nullptr, nullptr);
    if (!window){ fprintf(stderr,"Window failed\n"); glfwTerminate(); return 1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    float lightPos[]={0,100,0,1}, lightAmb[]={0.3f,0.3f,0.3f,1}, lightDif[]={0.9f,0.9f,0.85f,1};
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDif);

    Render::init();

    Game game; game.buildTable(); game.reset();

    printf("\n=== Pool Table Physics ===\n");
    printf("[CONTROLS]\n");
    printf("  Mouse-drag  orbit / aim       Scroll  zoom\n");
    printf("  Up / Down   shot power (hold)\n");
    printf("  Q           toggle tip-offset (spin) editor\n");
    printf("  E           ball-in-hand (levitate / place cue)\n");
    printf("  WASD        move dot (spin-edit) | move cue (levitating) | pan camera\n");
    printf("  Enter       shoot              R reset   Esc quit\n\n");

    using Clock = std::chrono::high_resolution_clock;
    auto lastTime = Clock::now(); double accumulator = 0;

    // --- control state ---
    bool  levitating   = false;
    bool  spinEdit     = false;
    float shotPowerMph = 17.0f;   const float MIN_MPH=0.1f, MAX_MPH=17.0f;
    float tipX = 0.f, tipY = 0.f; // normalised tip offset, |(x,y)| ≤ 1

    bool enterWas=false, eWas=false, rWas=false, qWas=false;

    while (!glfwWindowShouldClose(window)) {
        auto now = Clock::now();
        double frameDt = std::chrono::duration<double>(now-lastTime).count();
        lastTime = now; accumulator += frameDt;

        bool atRest = !game.inMotion;
        Vec3 aimDir = cam.aimDir();
        Vec3 rgtDir = cam.rightDir();

        // --- R reset ---
        bool rDown = glfwGetKey(window, GLFW_KEY_R)==GLFW_PRESS;
        if (rDown && !rWas){ game.reset(); levitating=false; spinEdit=false; }
        rWas=rDown;
        if (glfwGetKey(window, GLFW_KEY_ESCAPE)==GLFW_PRESS) break;

        // --- power (always adjustable) ---
        float pr = 6.0f*(float)frameDt;
        if (glfwGetKey(window, GLFW_KEY_UP)==GLFW_PRESS)   shotPowerMph += pr;
        if (glfwGetKey(window, GLFW_KEY_DOWN)==GLFW_PRESS) shotPowerMph -= pr;
        shotPowerMph = std::clamp(shotPowerMph, MIN_MPH, MAX_MPH);

        // --- Q: toggle spin editor (only at rest, not while levitating) ---
        bool qDown = glfwGetKey(window, GLFW_KEY_Q)==GLFW_PRESS;
        if (qDown && !qWas && atRest && !levitating) {
            spinEdit = !spinEdit;
            if (spinEdit)
                printf("[SPIN] Editor ON. WASD moves tip contact. Q to confirm.\n");
            else
                printf("[SPIN] Tip offset = (%+.2f, %+.2f)\n", tipX, tipY);
        }
        qWas=qDown;

        // --- E: ball-in-hand (only at rest, not while editing spin) ---
        bool eDown = glfwGetKey(window, GLFW_KEY_E)==GLFW_PRESS;
        if (eDown && !eWas && atRest && !spinEdit) {
            if (!levitating) {
                if (game.cueBall.pocketed){ game.cueBall.pocketed=false; game.cueBall.pos={0,0,0}; }
                levitating=true; game.cueBall.frozen=true;
                game.cueBall.vel={}; game.cueBall.angVel={};
                game.cueBall.pos.y = Table::BALL_RADIUS + 2.0f;
                printf("[PLACE] Ball-in-hand ON.\n");
            } else {
                levitating=false; game.cueBall.frozen=false;
                game.cueBall.pos.y = Table::BALL_RADIUS;
                game.cueBall.vel={}; game.cueBall.angVel={};
                printf("[PLACE] Cue placed at (%.2f, %.2f).\n",
                       game.cueBall.pos.x, game.cueBall.pos.z);
            }
        }
        eWas=eDown;

        // --- WASD routing ---
        bool wK=glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS;
        bool sK=glfwGetKey(window,GLFW_KEY_S)==GLFW_PRESS;
        bool aK=glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS;
        bool dK=glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS;

        if (spinEdit) {
            float rate = 1.4f*(float)frameDt;
            if (wK) tipY += rate;  if (sK) tipY -= rate;
            if (dK) tipX += rate;  if (aK) tipX -= rate;
            float m = std::sqrt(tipX*tipX + tipY*tipY);
            if (m > 1.f){ tipX/=m; tipY/=m; }            // clamp to disk
        }
        else if (levitating) {
            float mv = 15.0f*(float)frameDt; Vec3 dp{};
            if (wK) dp+=aimDir; if (sK) dp-=aimDir;
            if (dK) dp+=rgtDir; if (aK) dp-=rgtDir;
            game.cueBall.pos += dp*mv;
            float r=game.cueBall.radius, pad=0.2f;
            game.cueBall.pos.x = std::clamp(game.cueBall.pos.x, -Table::HALF_W+r+pad, Table::HALF_W-r-pad);
            game.cueBall.pos.z = std::clamp(game.cueBall.pos.z, -Table::HALF_L+r+pad, Table::HALF_L-r-pad);
            game.cueBall.pos.y = Table::BALL_RADIUS + 2.0f;
            if (!game.eightBall.pocketed) {
                Vec3 d{game.cueBall.pos.x-game.eightBall.pos.x,0,game.cueBall.pos.z-game.eightBall.pos.z};
                float dist=length(d), minD=2*r+0.1f;
                if (dist<minD && dist>1e-4f) game.cueBall.pos += normalize(d)*(minD-dist);
            }
        }
        else {
            float mv = 30.0f*(float)frameDt;
            if (wK) cam.target+=aimDir*mv; if (sK) cam.target-=aimDir*mv;
            if (dK) cam.target+=rgtDir*mv; if (aK) cam.target-=rgtDir*mv;
        }

        // --- Enter: shoot (blocked while editing spin or levitating) ---
        bool enterDown = glfwGetKey(window,GLFW_KEY_ENTER)==GLFW_PRESS
                      || glfwGetKey(window,GLFW_KEY_KP_ENTER)==GLFW_PRESS;
        if (enterDown && !enterWas && atRest && !levitating && !spinEdit && !game.cueBall.pocketed) {
            game.shoot(aimDir, shotPowerMph, tipX, tipY);
        }
        enterWas=enterDown;

        // --- physics ---
        while (accumulator >= game.config.dt){ game.step(); accumulator -= game.config.dt; }

        if (game.inMotion && game.ballsAtRest()) {
            game.inMotion=false;
            printf("[SETTLED] Time elapsed: %.2f s  (%d steps, avg %.4f ms/step)\n",
                   game.elapsedSimTime(),
                   game.physicsSteps - game.shotStartStep,
                   game.physicsTime / game.physicsSteps);
        }

        // --- render ---
        int w,h; glfwGetFramebufferSize(window,&w,&h);
        glViewport(0,0,w,h);
        glClearColor(0.12f,0.12f,0.15f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        Render::perspective(50.0f*Render::PI/180.0f,(float)w/h,1.0f,500.0f);
        glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
        Vec3 eye = cam.target + Vec3(
            std::cos(cam.pitch)*std::sin(cam.yaw),
            std::sin(cam.pitch),
            std::cos(cam.pitch)*std::cos(cam.yaw)) * cam.dist;
        Render::lookAt(eye, cam.target, {0,1,0});
        glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

        Render::drawTable(game.cushions, game.pockets);
        Render::drawCueBall(game.cueBall);
        Render::drawEightBall(game.eightBall);

        float power01 = (shotPowerMph-MIN_MPH)/(MAX_MPH-MIN_MPH);
        if (atRest && !game.cueBall.pocketed)
            Render::drawAimArrow(game.cueBall, aimDir, power01);

        Render::beginHUD(w,h);
        Render::drawStrengthBar(power01);
        Render::drawSpinDiagram(tipX, tipY, spinEdit);
        Render::endHUD();

        char title[160];
        const char* mode = spinEdit ? "[SPIN EDIT] "
                         : levitating ? "[PLACING] "
                         : atRest ? "[READY] " : "[IN MOTION] ";
        snprintf(title,sizeof(title),
                 "Pool  |  Power %.2f mph  |  Tip (%+.2f, %+.2f)  %s%s",
                 shotPowerMph, tipX, tipY, mode,
                 game.cueBall.pocketed ? "(cue pocketed — press E) " : "");
        glfwSetWindowTitle(window, title);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    Render::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}