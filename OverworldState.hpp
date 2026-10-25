#pragma once
#include "GameState.hpp"
#include "TileMap.hpp"
#include "Player.hpp"
#include "MusicManager.hpp"
#include "raylib.h"

// The main "walking around the world" state. Owns the map, the
// player, and the camera that follows them.
class OverworldState : public GameState {
public:
    OverworldState();
    ~OverworldState(); // Added destructor for audio cleanup

    void Update(float dt) override;
    void Draw() override;

private:
    TileMap map;
    Player player;
    Camera2D camera{};
    MusicManager music;
    float battleCooldown = 0.0f; // stops a fled-from battle trigger re-firing instantly

    // Registers every group -> track path the game knows about, and
    // starts/keeps playing whatever group the currently loaded map asks for.
    void RegisterMusicGroups();
    void SyncMusicToCurrentMap();
};