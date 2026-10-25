#include "raylib.h"
#include "StateManager.hpp"
#include "OverworldState.hpp"
#include <memory>

int main() {
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();

    InitWindow(screenWidth, screenHeight, "2D RPG Engine (raylib + C++)");
    SetTargetFPS(60);

    StateManager stateManager;
    stateManager.Push(std::make_unique<OverworldState>());

    while (!WindowShouldClose() && !stateManager.Empty()) {
        float dt = GetFrameTime();
        stateManager.Update(dt);

        BeginDrawing();
        ClearBackground(BLACK);
        stateManager.Draw();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
