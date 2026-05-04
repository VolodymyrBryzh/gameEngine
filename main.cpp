#include "raylib.h"
#include "raymath.h"
#include "perlin_noise.h"
#include <cmath>
#include <vector>

const int WORLD_SIZE = 512; // Гігантський світ
const float WORLD_SCALE = 1.0f;

struct Player {
    Vector3 position;
    Vector3 velocity;
    float velocityY;
    bool isGrounded;
};

int main() {
    // Ініціалізація
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Survival Engine C++ | Massive Realistic Terrain");

    PerlinNoise pn;
    
    // Створення Mesh ландшафту
    Mesh mesh = { 0 };
    mesh.triangleCount = (WORLD_SIZE - 1) * (WORLD_SIZE - 1) * 2;
    mesh.vertexCount = mesh.triangleCount * 3;
    mesh.vertices = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = (unsigned char *)MemAlloc(mesh.vertexCount * 4 * sizeof(unsigned char));

    auto GetHeight = [&](float x, float z) {
        if (x < 0 || x >= WORLD_SIZE || z < 0 || z >= WORLD_SIZE) return 0.0f;
        return pn.fbm(x * 0.01f, z * 0.01f, 6) * 80.0f;
    };

    int vIndex = 0;
    for (int x = 0; x < WORLD_SIZE - 1; x++) {
        for (int z = 0; z < WORLD_SIZE - 1; z++) {
            // Вершини для двох трикутників (один квадрат сітки)
            Vector3 v1 = { (float)x, GetHeight((float)x, (float)z), (float)z };
            Vector3 v2 = { (float)x + 1, GetHeight((float)x + 1, (float)z), (float)z };
            Vector3 v3 = { (float)x, GetHeight((float)x, (float)z + 1), (float)z + 1 };
            Vector3 v4 = { (float)x + 1, GetHeight((float)x + 1, (float)z + 1), (float)z + 1 };

            auto AddTriangle = [&](Vector3 p1, Vector3 p2, Vector3 p3) {
                Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p2, p1), Vector3Subtract(p3, p1)));
                
                Vector3 pts[3] = { p1, p2, p3 };
                for (int i = 0; i < 3; i++) {
                    mesh.vertices[vIndex * 3] = pts[i].x;
                    mesh.vertices[vIndex * 3 + 1] = pts[i].y;
                    mesh.vertices[vIndex * 3 + 2] = pts[i].z;
                    
                    mesh.normals[vIndex * 3] = normal.x;
                    mesh.normals[vIndex * 3 + 1] = normal.y;
                    mesh.normals[vIndex * 3 + 2] = normal.z;

                    // Колір залежно від висоти та освітлення (псевдо-shading)
                    float light = Vector3DotProduct(normal, Vector3Normalize({ 0.5f, 1.0f, 0.2f }));
                    if (light < 0.3f) light = 0.3f; // Мінімальне світло

                    Color col = DARKGREEN;
                    if (pts[i].y > 60.0f) col = WHITE;
                    else if (pts[i].y > 40.0f) col = GRAY;
                    else if (pts[i].y < 12.0f) col = BEIGE;

                    mesh.colors[vIndex * 4] = (unsigned char)(col.r * light);
                    mesh.colors[vIndex * 4 + 1] = (unsigned char)(col.g * light);
                    mesh.colors[vIndex * 4 + 2] = (unsigned char)(col.b * light);
                    mesh.colors[vIndex * 4 + 3] = 255;
                    vIndex++;
                }
            };

            AddTriangle(v1, v3, v2);
            AddTriangle(v2, v3, v4);
        }
    }

    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);

    Player player = { { 256.0f, 100.0f, 256.0f }, { 0, 0, 0 }, 0.0f, false };
    Camera3D camera = { { 0 }, { 0 }, { 0, 1, 0 }, 60.0f, CAMERA_PERSPECTIVE };

    float walkSpeed = 1.39f;  // 5 км/год (в м/с)
    float sprintSpeed = 4.17f; // 15 км/год (в м/с)
    float bobbingTimer = 0.0f;

    SetTargetFPS(60);
    DisableCursor();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        
        // --- ОГЛЯД МИШЕЮ ---
        Vector2 mouseDelta = GetMouseDelta();
        static float pitch = 0.0f, yaw = 0.0f;
        yaw -= mouseDelta.x * 0.003f; pitch -= mouseDelta.y * 0.003f;
        if (pitch > 1.5f) pitch = 1.5f; if (pitch < -1.5f) pitch = -1.5f;

        // --- ЛОГІКА РУХУ ---
        bool isSprinting = IsKeyDown(KEY_LEFT_SHIFT);
        float targetSpeed = isSprinting ? sprintSpeed : walkSpeed;
        
        Vector3 forward = { sinf(yaw), 0, cosf(yaw) };
        Vector3 right = { cosf(yaw), 0, -sinf(yaw) };
        Vector3 moveDir = { 0, 0, 0 };

        if (IsKeyDown(KEY_W)) moveDir = Vector3Add(moveDir, forward);
        if (IsKeyDown(KEY_S)) moveDir = Vector3Subtract(moveDir, forward);
        if (IsKeyDown(KEY_A)) moveDir = Vector3Add(moveDir, right);
        if (IsKeyDown(KEY_D)) moveDir = Vector3Subtract(moveDir, right);

        if (Vector3Length(moveDir) > 0) {
            moveDir = Vector3Normalize(moveDir);
            player.velocity.x = Lerp(player.velocity.x, moveDir.x * targetSpeed, dt * 5.0f);
            player.velocity.z = Lerp(player.velocity.z, moveDir.z * targetSpeed, dt * 5.0f);
            bobbingTimer += dt * (isSprinting ? 12.0f : 8.0f);
        } else {
            player.velocity.x = Lerp(player.velocity.x, 0, dt * 5.0f);
            player.velocity.z = Lerp(player.velocity.z, 0, dt * 5.0f);
            bobbingTimer = Lerp(bobbingTimer, 0, dt * 5.0f);
        }

        player.position.x += player.velocity.x * dt; 
        player.position.z += player.velocity.z * dt;

        // Фізика
        player.velocityY -= 0.6f * dt;
        player.position.y += player.velocityY;

        float h = GetHeight(player.position.x, player.position.z);
        float bobOffset = sinf(bobbingTimer) * 0.15f;

        if (player.position.y < h + 2.0f) {
            player.position.y = h + 2.0f;
            player.velocityY = 0;
            player.isGrounded = true;
        } else player.isGrounded = false;

        if (IsKeyPressed(KEY_SPACE) && player.isGrounded) player.velocityY = 0.25f;

        camera.position = { player.position.x, player.position.y + bobOffset, player.position.z };
        camera.target = { camera.position.x + sinf(yaw), camera.position.y + sinf(pitch), camera.position.z + cosf(yaw) };

        BeginDrawing();
            ClearBackground(SKYBLUE);
            BeginMode3D(camera);
                DrawModel(model, { 0, 0, 0 }, 1.0f, WHITE);
                DrawPlane({ 256, 10.0f, 256 }, { 2000, 2000 }, { 0, 121, 241, 150 }); 
            EndMode3D();
            DrawFPS(10, 10);
            DrawText(TextFormat("Speed: %.1f km/h", Vector3Length({player.velocity.x, 0, player.velocity.z}) * 3.6f), 10, 40, 20, WHITE);
            DrawText("Hold SHIFT to SPRINT", 10, 70, 20, WHITE);
        EndDrawing();
    }
    UnloadModel(model);
    CloseWindow();
    return 0;
}
