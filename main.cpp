#include "raylib.h"
#include <cmath>

struct Player {
    Vector3 position;
    float velocityY;
    bool isGrounded;
    float speed;
};

int main() {
    // Ініціалізація
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Survival Engine C++ | Movement System");

    Player player = { { 0.0f, 10.0f, 0.0f }, 0.0f, false, 0.15f };

    Camera3D camera = { 0 };
    camera.position = player.position;
    camera.target = Vector3{ 1.0f, 10.0f, 1.0f };
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);
    DisableCursor();

    while (!WindowShouldClose()) {
        // --- ЛОГІКА РУХУ ---
        Vector3 forward = { 
            camera.target.x - camera.position.x, 
            0, 
            camera.target.z - camera.position.z 
        };
        float length = sqrtf(forward.x * forward.x + forward.z * forward.z);
        if (length != 0) { forward.x /= length; forward.z /= length; }

        Vector3 right = { -forward.z, 0, forward.x };

        // Рух WASD
        if (IsKeyDown(KEY_W)) {
            player.position.x += forward.x * player.speed;
            player.position.z += forward.z * player.speed;
        }
        if (IsKeyDown(KEY_S)) {
            player.position.x -= forward.x * player.speed;
            player.position.z -= forward.z * player.speed;
        }
        if (IsKeyDown(KEY_A)) {
            player.position.x -= right.x * player.speed;
            player.position.z -= right.z * player.speed;
        }
        if (IsKeyDown(KEY_D)) {
            player.position.x += right.x * player.speed;
            player.position.z += right.z * player.speed;
        }

        // Гравітація та Стрибок
        player.velocityY -= 0.01f; // Сила тяжіння
        player.position.y += player.velocityY;

        // Колізія з ландшафтом (спрощена)
        float floorHeight = 0.0f;
        int gridX = (int)round(player.position.x / 3.0f);
        int gridZ = (int)round(player.position.z / 3.0f);
        
        if (gridX >= -15 && gridX < 15 && gridZ >= -15 && gridZ < 15) {
            floorHeight = (sin((float)gridX * 3.0f * 0.2f) + cos((float)gridZ * 3.0f * 0.2f)) * 3.0f + 2.0f;
        }

        if (player.position.y < floorHeight + 2.0f) { // 2.0f - зріст гравця
            player.position.y = floorHeight + 2.0f;
            player.velocityY = 0;
            player.isGrounded = true;
        } else {
            player.isGrounded = false;
        }

        if (IsKeyPressed(KEY_SPACE) && player.isGrounded) {
            player.velocityY = 0.2f; // Сила стрибка
        }

        // Оновлення камери
        UpdateCamera(&camera, CAMERA_FIRST_PERSON); 
        camera.position = player.position; // Прив'язуємо камеру до гравця

        BeginDrawing();
            ClearBackground(Color{ 135, 206, 235, 255 });

            BeginMode3D(camera);
                // Океан
                DrawPlane({ 0, 0, 0 }, { 1000, 1000 }, Color{ 0, 105, 148, 255 });

                // Острів
                for (int x = -15; x < 15; x++) {
                    for (int z = -15; z < 15; z++) {
                        float fx = (float)x * 3.0f;
                        float fz = (float)z * 3.0f;
                        float h = (sin(fx * 0.2f) + cos(fz * 0.2f)) * 3.0f + 2.0f;
                        
                        Color col = DARKGREEN;
                        if (h > 4.5f) col = GRAY;
                        else if (h < 1.0f) col = BEIGE;

                        DrawCube({ fx, h / 2.0f, fz }, 3.0f, h, 3.0f, col);
                        DrawCubeWires({ fx, h / 2.0f, fz }, 3.0f, h, 3.0f, Color{ 0, 0, 0, 40 });
                    }
                }
            EndMode3D();

            DrawFPS(10, 10);
            DrawText("WASD: Move | SPACE: Jump | MOUSE: Look", 10, 40, 20, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
