#include "raylib.h"
#include "raymath.h"
#include "perlin_noise.h"
#include "chunk_manager.h"
#include "tree_assets.h"
#include "chunk.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>

#ifdef _WIN32
  #include <direct.h>
  #define MKDIR(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #define MKDIR(p) mkdir(p, 0777)
#endif

struct Player {
    Vector3 position;
    Vector3 velocity;
    float   velocityY;
    bool    isGrounded;
};

static constexpr const char* SAVE_FILE = "save/world.dat";

static uint32_t LoadOrCreateSeed(int argc, char** argv) {
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--seed") == 0)
            return (uint32_t)atoi(argv[i + 1]);

    std::ifstream f(SAVE_FILE);
    if (f.is_open()) { uint32_t s; f >> s; return s; }

    return (uint32_t)time(nullptr);
}

static void SaveSeed(uint32_t seed) {
    MKDIR("save");
    std::ofstream f(SAVE_FILE);
    f << seed;
}

static Vector3 FindGreenStart(const PerlinNoise& pn) {
    const float TWO_PI = 2.0f * PI;
    for (int r = 0; r <= 512; r += 16) {
        for (int a = 0; a < 16; a++) {
            float angle = a * (TWO_PI / 16.0f);
            float x = cosf(angle) * r;
            float z = sinf(angle) * r;
            float h = SampleWorldHeight(pn, x, z);
            if (h >= 12.0f && h <= 38.0f)
                return { x, h + 2.0f, z };
        }
    }
    float h = SampleWorldHeight(pn, 0, 0);
    return { 0.0f, h + 2.0f, 0.0f };
}

int main(int argc, char** argv) {
    uint32_t seed = LoadOrCreateSeed(argc, argv);
    SaveSeed(seed);

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Survival Game");

    PerlinNoise  pn(seed);
    ChunkManager world(pn);

    TreeAssets treeAssets = BuildTreeMesh();
    world.SetTreeAssets(treeAssets);

    Vector3 startPos = FindGreenStart(pn);
    world.LoadImmediate(startPos, 2);

    Player   player = { startPos, { 0, 0, 0 }, 0.0f, false };
    Camera3D camera = { {}, {}, { 0, 1, 0 }, 60.0f, CAMERA_PERSPECTIVE };

    float walkSpeed    = 1.39f;
    float sprintSpeed  = 4.17f;
    float bobbingTimer = 0.0f;
    float pitch = 0.0f, yaw = 0.0f;

    SetTargetFPS(60);
    DisableCursor();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        
        // --- Input ---
        Vector2 md = GetMouseDelta();
        yaw   -= md.x * 0.003f;
        pitch -= md.y * 0.003f;
        if (pitch >  1.5f) pitch =  1.5f;
        if (pitch < -1.5f) pitch = -1.5f;

        bool sprinting = IsKeyDown(KEY_LEFT_SHIFT);
        float speed    = sprinting ? sprintSpeed : walkSpeed;

        Vector3 forward = { sinf(yaw), 0, cosf(yaw) };
        Vector3 right   = { cosf(yaw), 0, -sinf(yaw) };
        Vector3 moveDir = { 0, 0, 0 };
        if (IsKeyDown(KEY_W)) moveDir = Vector3Add(moveDir, forward);
        if (IsKeyDown(KEY_S)) moveDir = Vector3Subtract(moveDir, forward);
        if (IsKeyDown(KEY_A)) moveDir = Vector3Add(moveDir, right);
        if (IsKeyDown(KEY_D)) moveDir = Vector3Add(moveDir, right);

        if (Vector3Length(moveDir) > 0) {
            moveDir = Vector3Normalize(moveDir);
            player.velocity.x = Lerp(player.velocity.x, moveDir.x * speed, dt * 5.0f);
            player.velocity.z = Lerp(player.velocity.z, moveDir.z * speed, dt * 5.0f);
            bobbingTimer += dt * (sprinting ? 12.0f : 8.0f);
        } else {
            player.velocity.x = Lerp(player.velocity.x, 0, dt * 5.0f);
            player.velocity.z = Lerp(player.velocity.z, 0, dt * 5.0f);
            bobbingTimer = Lerp(bobbingTimer, 0, dt * 5.0f);
        }

        // --- Physics ---
        player.position.x += player.velocity.x * dt;
        player.position.z += player.velocity.z * dt;
        world.ApplyTreeCollision(player.position);
        player.velocityY  -= 0.6f * dt;
        player.position.y += player.velocityY;

        float ground = world.GetHeight(player.position.x, player.position.z);
        if (player.position.y < ground + 2.0f) {
            player.position.y = ground + 2.0f;
            player.velocityY  = 0;
            player.isGrounded = true;
        } else {
            player.isGrounded = false;
        }

        if (IsKeyPressed(KEY_SPACE) && player.isGrounded) player.velocityY = 0.25f;

        float bob = sinf(bobbingTimer) * 0.15f;
        camera.position = { player.position.x, player.position.y + bob, player.position.z };
        camera.target   = {
            camera.position.x + sinf(yaw),
            camera.position.y + sinf(pitch),
            camera.position.z + cosf(yaw)
        };
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector3 dir = Vector3Subtract(camera.target, camera.position);
            dir = Vector3Normalize(dir);
            world.TryHitTree(camera.position, dir);
        }
        
        world.Update(player.position);

        BeginDrawing();
            ClearBackground(SKYBLUE);
            BeginMode3D(camera);
                world.Render(player.position, 120.0f, 100.0f);
                DrawPlane({ player.position.x, 10.0f, player.position.z }, { 10000, 10000 }, { 0, 121, 241, 150 });
            EndMode3D();

            DrawCircle(GetScreenWidth()/2, GetScreenHeight()/2, 2, WHITE);
            DrawFPS(10, 10);
            DrawText(TextFormat("Pos: %.0f, %.0f", player.position.x, player.position.z), 10, 40, 20, WHITE);
        EndDrawing();
    }

    world.Shutdown();
    treeAssets.Unload();
    CloseWindow();
    return 0;
}
