#include "raylib.h"
#include "raymath.h"
#include "perlin_noise.h"
#include "chunk_manager.h"
#include <cmath>

struct Player {
    Vector3 position;
    Vector3 velocity;
    float   velocityY;
    bool    isGrounded;
};

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Survival Engine | Infinite Procedural World");

    PerlinNoise  pn;
    ChunkManager world(pn);

    // Load 5×5 chunks synchronously before first frame so player doesn't fall through
    Vector3 startPos = { 64.0f, 100.0f, 64.0f };
    world.LoadImmediate(startPos, 2);

    Player   player = { startPos, { 0, 0, 0 }, 0.0f, false };
    Camera3D camera = { {}, {}, { 0, 1, 0 }, 60.0f, CAMERA_PERSPECTIVE };

    float walkSpeed    = 1.39f;   // 5 km/h
    float sprintSpeed  = 4.17f;   // 15 km/h
    float bobbingTimer = 0.0f;
    float pitch = 0.0f, yaw = 0.0f;

    SetTargetFPS(60);
    DisableCursor();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Stream up to 3 new chunks per frame, unload distant ones
        world.Update(player.position);

        // --- Mouse look ---
        Vector2 md = GetMouseDelta();
        yaw   -= md.x * 0.003f;
        pitch -= md.y * 0.003f;
        if (pitch >  1.5f) pitch =  1.5f;
        if (pitch < -1.5f) pitch = -1.5f;

        // --- Movement ---
        bool sprinting = IsKeyDown(KEY_LEFT_SHIFT);
        float speed    = sprinting ? sprintSpeed : walkSpeed;

        Vector3 forward = { sinf(yaw), 0, cosf(yaw) };
        Vector3 right   = { cosf(yaw), 0, -sinf(yaw) };
        Vector3 moveDir = { 0, 0, 0 };
        if (IsKeyDown(KEY_W)) moveDir = Vector3Add(moveDir, forward);
        if (IsKeyDown(KEY_S)) moveDir = Vector3Subtract(moveDir, forward);
        if (IsKeyDown(KEY_A)) moveDir = Vector3Add(moveDir, right);
        if (IsKeyDown(KEY_D)) moveDir = Vector3Subtract(moveDir, right);

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

        player.position.x += player.velocity.x * dt;
        player.position.z += player.velocity.z * dt;
        player.velocityY  -= 0.6f * dt;
        player.position.y += player.velocityY;

        float ground = world.GetHeight(player.position.x, player.position.z);
        float bob    = sinf(bobbingTimer) * 0.15f;

        if (player.position.y < ground + 2.0f) {
            player.position.y = ground + 2.0f;
            player.velocityY  = 0;
            player.isGrounded = true;
        } else {
            player.isGrounded = false;
        }

        if (IsKeyPressed(KEY_SPACE) && player.isGrounded) player.velocityY = 0.25f;

        camera.position = { player.position.x, player.position.y + bob, player.position.z };
        camera.target   = {
            camera.position.x + sinf(yaw),
            camera.position.y + sinf(pitch),
            camera.position.z + cosf(yaw)
        };

        BeginDrawing();
            ClearBackground(SKYBLUE);
            BeginMode3D(camera);
                world.Render(player.position, 120.0f, 100.0f);
                // Large water plane centered on player to always cover horizon
                DrawPlane({ player.position.x, 10.0f, player.position.z },
                          { 10000, 10000 }, { 0, 121, 241, 150 });
            EndMode3D();

            DrawFPS(10, 10);
            DrawText(TextFormat("Speed: %.1f km/h",
                Vector3Length({ player.velocity.x, 0, player.velocity.z }) * 3.6f), 10, 40, 20, WHITE);
            DrawText(TextFormat("Chunks loaded: %d", world.ChunkCount()), 10, 70, 20, WHITE);
            DrawText(TextFormat("Pos: %.0f / %.1f / %.0f",
                player.position.x, player.position.y, player.position.z), 10, 100, 20, WHITE);
            DrawText("WASD + mouse | SHIFT sprint | SPACE jump", 10, 130, 20, WHITE);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
