#include "raylib.h"
#include "raymath.h"
#include "perlin_noise.h"
#include "chunk_manager.h"
#include "tree_assets.h"
#include "chunk.h"
#include "editor.h"
#include <cmath>
#include <cstdlib>
#include <ctime>

int main() {
    uint32_t seed = (uint32_t)time(nullptr);

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1600, 900, "Survival Engine Editor");

    PerlinNoise  pn(seed);
    ChunkManager world(pn);

    TreeAssets treeAssets = BuildTreeMesh();
    world.SetTreeAssets(treeAssets);

    Editor editor;
    editor.Init();

    Camera3D camera = { { 10, 10, 10 }, { 0, 0, 0 }, { 0, 1, 0 }, 45.0f, CAMERA_PERSPECTIVE };
    
    SetTargetFPS(144);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        
        // В режимі редактора ми можемо вільно рухати камеру (спрощено)
        if (!ImGui::GetIO().WantCaptureMouse) {
            if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
                UpdateCamera(&camera, CAMERA_FREE);
            }
        }

        world.Update(camera.position);

        BeginDrawing();
            ClearBackground(DARKGRAY);
            BeginMode3D(camera);
                world.Render(camera.position, 200.0f, 150.0f);
                DrawGrid(20, 1.0f);
            EndMode3D();

            editor.Draw(world, camera);
        EndDrawing();
    }

    editor.Shutdown();
    world.Shutdown();
    treeAssets.Unload();
    CloseWindow();
    return 0;
}
