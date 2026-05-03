#include "raylib.h"
#include <vector>
#include <cmath>

int main() {
    // Ініціалізація
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_MSAA_4X_HINT); // Вмикаємо згладжування
    InitWindow(screenWidth, screenHeight, "Survival Engine C++ | Visual Studio");

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 20.0f, 20.0f, 20.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);
    DisableCursor();

    while (!WindowShouldClose()) {
        UpdateCamera(&camera, CAMERA_FIRST_PERSON);

        BeginDrawing();
            ClearBackground(Color{ 135, 206, 235, 255 }); // Гарне небо

            BeginMode3D(camera);
                
                // Океан
                DrawPlane({ 0, 0, 0 }, { 1000, 1000 }, Color{ 0, 105, 148, 255 });

                // Острів
                for (int x = -15; x < 15; x++) {
                    for (int z = -15; z < 15; z++) {
                        float fx = (float)x * 3.0f;
                        float fz = (float)z * 3.0f;
                        // Проста математична генерація пагорбів
                        float h = (sin(fx * 0.2f) + cos(fz * 0.2f)) * 3.0f + 2.0f;
                        
                        Color col = DARKGREEN;
                        if (h > 4.5f) col = GRAY;
                        else if (h < 1.0f) col = BEIGE;

                        DrawCube({ fx, h / 2.0f, fz }, 3.0f, h, 3.0f, col);
                        DrawCubeWires({ fx, h / 2.0f, fz }, 3.0f, h, 3.0f, Color{ 0, 0, 0, 50 });
                    }
                }

                DrawSphere({ 100, 150, 100 }, 10.0f, YELLOW); // Сонце

            EndMode3D();

            DrawFPS(10, 10);
            DrawText("MOVE: WASD | ESC: EXIT", 10, 40, 20, DARKGRAY);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
