#pragma once
#include "raylib.h"
#include <vector>
#include <string>
#include <unordered_map>

constexpr int TILE_SIZE = 32;
static std::string NormalizeJSON(const std::string& in);
struct TileDef {
    Texture2D texture;   
    Color color;         
    bool solid = false;
    bool overhang = false;
    float visualHeight = 0.0f;
    bool visible = true;
    std::string footstepType = ""; // Custom Tiled property (e.g. "grass", "stone")
};
// Represents a trigger area created in Tiled Object Layers
struct MapTrigger {
    std::string name;
    std::string type;          // "warp", "event", "damage", etc.
    Rectangle bounds;          // Position and size in world pixels
    
    // Custom trigger properties
    std::string targetMap;     // e.g. "house_interior.json"
    Vector2 targetPos{ 0, 0 }; // e.g. Spawn position in pixels
    std::string eventId;       // e.g. "open_chest" or "dialogue_npc1"
    std::string enemyId;       // type "battle": which enemy to fight, e.g. "crow"
    int id = 0;                // Tiled's unique object id (stable within a map)
    bool triggerOnce = false;
    bool hasFired = false;
};
class TileMap {
public:
    std::string musicGroup = "";
    TileMap() = default;
    ~TileMap(); // Frees cached tileset textures (see tilesetCache below)

    void LoadPlaceholder(int width, int height);
    bool LoadTilesetImage(const std::string& imagePath, int tileWidth, int tileHeight, int firstGID);
    bool LoadFromTiledJSON(const std::string& path);
    void UnloadAudio();
    // Checks player's bounding box against all active map triggers
    std::vector<MapTrigger*> GetCollidingTriggers(Rectangle playerBounds);
    // Marks a trigger as spent by its Tiled object id (used to remove an
    // enemy after you beat it; safe to call even if the map was reloaded).
    void MarkTriggerFired(int triggerId);
    // Register a sound file for a footstep name (e.g., RegisterFootstepSound("grass", "assets/sounds/step_grass.wav"))
    void RegisterFootstepSound(const std::string& type, const std::string& soundFilePath);
    
    // Triggers the sound corresponding to the top-most tile at pixelPos
    void PlayFootstepAtPixel(Vector2 pixelPos) const;

    void Draw() const;
    void DrawGround() const;
    void DrawOverhang() const;

    bool IsSolidAtPixel(Vector2 pixelPos) const;

    int WidthInTiles() const { return width; }
    int HeightInTiles() const { return height; }
    int WidthInPixels() const { return width * TILE_SIZE; }
    int HeightInPixels() const { return height * TILE_SIZE; }

private:
    int width = 0;
    int height = 0;
    
    std::vector<std::vector<int>> layers; 
    std::vector<TileDef> tileDefs;       
    std::unordered_map<std::string, Sound> footstepSounds; // Sound library mapped by type name
    std::vector<MapTrigger> triggers; // Loaded triggers from Tiled

    // One entry per unique tileset IMAGE PATH (not per map). Warping between
    // maps that reuse the same tileset - or warping back to a map you were
    // just on - reuses these instead of re-decoding the PNG and re-uploading
    // every tile's texture to the GPU from scratch on every load.
    struct CachedTileset {
        std::vector<Texture2D> tileTextures; // indexed by local tile id (0..tilecount-1)
    };
    std::unordered_map<std::string, CachedTileset> tilesetCache;

    int TileAt(int layerIdx, int x, int y) const;
    std::string GetFootstepTypeAtPixel(Vector2 pixelPos) const;
};