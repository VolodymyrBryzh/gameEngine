#pragma once
#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"
#include "chunk_manager.h"

class Editor {
public:
    bool showEditor = true;
    TreeData* selectedTree = nullptr;

    void Init() {
        rlImGuiSetup(true); // true means dark theme
    }

    void Shutdown() {
        rlImGuiShutdown();
    }

    void Draw(ChunkManager& world, Camera3D& camera) {
        if (!showEditor) return;

        // Логіка вибору об'єкта мишкою
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !ImGui::GetIO().WantCaptureMouse) {
            Ray ray = GetMouseRay(GetMousePosition(), camera);
            selectedTree = world.PickTree(ray);
        }

        rlImGuiBegin();

        // Візуальна підсвітка вибраного об'єкта
        if (selectedTree) {
            BeginMode3D(camera);
                DrawCapsuleWires(
                    { selectedTree->pos.x, selectedTree->pos.y, selectedTree->pos.z },
                    { selectedTree->pos.x, selectedTree->pos.y + selectedTree->trunkH, selectedTree->pos.z },
                    selectedTree->trunkR + 0.1f, 8, 4, GREEN);
            EndMode3D();
        }

        // Основне вікно редактора (Меню)
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Scene")) { /* TODO */ }
                if (ImGui::MenuItem("Exit")) { /* TODO */ }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Editor Mode", NULL, &showEditor);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Вікно ієрархії (Hierarchy)
        ImGui::Begin("Hierarchy");
        if (ImGui::CollapsingHeader("Chunks", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Active Chunks Count: %d", world.ChunkCount());
        }
        if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (selectedTree) {
                ImGui::Text("Type: %s", (selectedTree->type == OAK ? "Oak" : (selectedTree->type == SPRUCE ? "Spruce" : "Cactus")));
            } else {
                ImGui::Text("Nothing selected");
            }
        }
        ImGui::End();

        // Вікно інспектора (Inspector)
        ImGui::Begin("Inspector");
        if (selectedTree) {
            ImGui::Text("Object: Tree");
            ImGui::Separator();
            
            ImGui::Text("Transform");
            ImGui::DragFloat3("Position", &selectedTree->pos.x, 0.1f);
            ImGui::DragFloat("Scale", &selectedTree->scale, 0.05f, 0.1f, 5.0f);
            ImGui::DragFloat("Rotation", &selectedTree->rotation, 1.0f);
            
            ImGui::Separator();
            ImGui::Text("Tree Properties");
            ImGui::DragFloat("Trunk Height", &selectedTree->trunkH, 0.1f, 1.0f, 10.0f);
            ImGui::DragFloat("Trunk Radius", &selectedTree->trunkR, 0.01f, 0.05f, 1.0f);
            ImGui::DragInt("HP", &selectedTree->hp, 1, 0, 100);
            
            if (ImGui::Button("Delete Object")) {
                selectedTree->fallen = true; // Simple way to "delete" for now
                selectedTree = nullptr;
            }
        } else {
            ImGui::Text("Select an object in 3D view to edit");
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Camera Info", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Camera Position", &camera.position.x, 0.1f);
                ImGui::DragFloat3("Camera Target", &camera.target.x, 0.1f);
            }
        }
        ImGui::End();

        // Вікно статистики
        ImGui::Begin("Statistics");
        ImGui::Text("FPS: %d", GetFPS());
        ImGui::End();

        rlImGuiEnd();
    }
};
