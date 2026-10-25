#include "OverworldState.hpp"
#include "raylib.h"
#include "raymath.h"
#include "StateManager.hpp"
#include "CombatState.hpp"
#include <memory>
#include <algorithm>

OverworldState::OverworldState() : player(&map) {
    // Initialize Raylib Audio Device
    InitAudioDevice();

    if (!map.LoadFromTiledJSON("assets/maps/map.json")) {
        TraceLog(LOG_WARNING, "OverworldState: map.json failed to load - using placeholder map "
                               "so the player isn't stuck in a 0x0 world.");
        map.LoadPlaceholder(20, 15);
    }

    // Register footstep sounds (matching the "footstep" string property in Tiled)
    //map.RegisterFootstepSound("grass", "assets/sounds/step_grass.wav");
   // map.RegisterFootstepSound("stone", "assets/sounds/step_stone.wav");
    map.RegisterFootstepSound("wood",  "wood.mp3");

    // Register ambient music tracks (matching the "musicGroup" string
    // property set on the Map itself in Tiled, not on a layer/tile/object).
    RegisterMusicGroups();
    SyncMusicToCurrentMap();

    // Drop the player somewhere walkable in the middle of the map.
    player.position = { 4 * TILE_SIZE, 4 * TILE_SIZE };

    camera.zoom = 1.0f;
    camera.rotation = 0.0f;
}

void OverworldState::RegisterMusicGroups() {
    // Each group name here must match a "musicGroup" property value you set
    // on a Map in Tiled. LoadMusicStream'd tracks loop by default in raylib,
    // so nothing extra is needed for looping.
    music.RegisterGroupTrack("overworld", "assets/audio/overworld.mp3");
    music.RegisterGroupTrack("0",   "assets/music/default.mp3");
   // music.RegisterGroupTrack("town",      "assets/music/town.ogg");
}

void OverworldState::SyncMusicToCurrentMap() {
    // SwitchToGroup already no-ops if we're asking for the group that's
    // already playing, so moving between two rooms that share the same
    // musicGroup (e.g. "dungeon") never restarts the track. Moving to a
    // room with a *different* musicGroup (or none) does restart/stop it.
    music.SwitchToGroup(map.musicGroup);
}

OverworldState::~OverworldState() {
    music.UnloadAll(); // must run before CloseAudioDevice()
    map.UnloadAudio();
    CloseAudioDevice();
}

void OverworldState::Update(float dt) {
    player.Update(dt);
    music.Update(); // Must be called every frame or the stream underruns
    battleCooldown = std::max(0.0f, battleCooldown - dt);

    // --- Random encounters from tile properties ---
    // Rolled once per footstep, using the tile under the player's feet.
    if (player.stepped)
    {
        player.stepped = false;
        std::string enemyId;
        Vector2 feet = { player.position.x + player.size.x / 2.0f,
                         player.position.y + player.size.y - 1.0f };
        if (battleCooldown <= 0.0f && manager && map.RollEncounterAtPixel(feet, enemyId))
        {
            battleCooldown = 2.0f; // breathing room after the fight ends
            manager->Push(std::make_unique<CombatState>(&player, enemyId));
        }
    }
    // --- Check for Triggers ---
    Rectangle playerBounds = player.GetCollider();
    auto activeTriggers = map.GetCollidingTriggers(playerBounds);
    for (MapTrigger* trig : activeTriggers)
    {
        
        // Warp / Map Transition
        if (trig->type == "warp")
        {
            if (!trig->targetMap.empty())
            {
                // IMPORTANT: copy these out by value first. 'trig' points into
                // map's own `triggers` vector, and LoadFromTiledJSON clears
                // that same vector as its first step - so passing
                // trig->targetMap through *by reference* would have us
                // reading from a std::string that gets destroyed mid-call.
                std::string targetMap = trig->targetMap;
                Vector2 targetPos = trig->targetPos;

                if (!map.LoadFromTiledJSON(targetMap)) {
                    TraceLog(LOG_ERROR, "OverworldState: warp target '%s' failed to load",
                              targetMap.c_str());
                } else {
                    player.position = targetPos; // Move player to destination
                    // Restarts music only if the new map's musicGroup differs
                    // from what's currently playing (see SyncMusicToCurrentMap).
                    SyncMusicToCurrentMap();
                }
                break; // Stop processing further triggers for this frame -
                       // 'activeTriggers' now contains dangling pointers
                       // into the reloaded map, so nothing after this point
                       // may dereference 'trig' again.
            }
        }
        // Battle: push a CombatState on top of this one. The StateManager
        // queues the push until this Update returns, and this state stops
        // receiving Update() calls until the battle pops itself off.
        else if (trig->type == "battle")
        {
            if (battleCooldown > 0.0f || !manager) continue;

            int triggerId = trig->id;
            std::string enemyId = trig->enemyId;
            bool once = trig->triggerOnce;
            battleCooldown = 1.5f; // grace period after returning (e.g. after fleeing)

            manager->Push(std::make_unique<CombatState>(
                &player, enemyId,
                [this, triggerId, once](bool won) {
                    // Only remove a one-time enemy if you actually beat it.
                    if (won && once) map.MarkTriggerFired(triggerId);
                }));
            break; // one battle per frame; skip the generic triggerOnce marking
        }
        // Custom Event / Cutscene
        else if (trig->type == "event")
        {
            TraceLog(LOG_INFO, "Event Triggered: %s", trig->eventId.c_str());
            // Add your dialogue or event logic here...
        }
        // Hazard / Trap
        else if (trig->type == "damage")
        {
            player.hp = std::max(0, player.hp - 1);
        }

        // Mark as fired if configured as a one-time trigger
        if (trig->triggerOnce)
        {
            trig->hasFired = true;
        }
    }

    // Keep the camera centered on the player, clamped so it doesn't
    // show past the edges of the map.
    camera.offset = { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };

    float halfW = GetScreenWidth() / 2.0f;
    float halfH = GetScreenHeight() / 2.0f;
    float targetX = player.position.x + player.size.x / 2.0f;
    float targetY = player.position.y + player.size.y / 2.0f;

    float mapW = static_cast<float>(map.WidthInPixels());
    float mapH = static_cast<float>(map.HeightInPixels());

    if (mapW > GetScreenWidth()) {
        targetX = Clamp(targetX, halfW, mapW - halfW);
    } else {
        targetX = mapW / 2.0f;
    }
    if (mapH > GetScreenHeight()) {
        targetY = Clamp(targetY, halfH, mapH - halfH);
    } else {
        targetY = mapH / 2.0f;
    }

    camera.target = { targetX, targetY };
}

void OverworldState::Draw() {
    BeginMode2D(camera);
    map.DrawGround();   // grass/floor - behind everything
    player.Draw();      // entities - on top of ground
    map.DrawOverhang(); // tree tops/wall tops - in front of entities FIXED ORDER
    EndMode2D();

    // Screen-space UI (HUD) goes outside BeginMode2D/EndMode2D.
    DrawText(TextFormat("HP: %d/%d  Lv.%d", player.hp, player.maxHp, player.level),
              10, 10, 20, WHITE);
    DrawFPS(GetScreenWidth() - 90, 10);
}