#include "TileMap.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <functional>

#define CUTE_TILED_IMPLEMENTATION
#include "cute_tiled.h"

TileMap::~TileMap()
{
    // tileDefs entries just hold copies of the Texture2D handles owned by
    // tilesetCache, so unloading happens here - once per unique tileset
    // image - rather than per map load (which previously leaked every
    // tileset's GPU textures on every warp, since tileDefs.clear() dropped
    // the handles without ever calling UnloadTexture on them).
    for (auto& entry : tilesetCache)
    {
        for (Texture2D& tex : entry.second.tileTextures)
        {
            if (tex.id != 0)
                UnloadTexture(tex);
        }
    }
    tilesetCache.clear();
}

bool TileMap::LoadFromTiledJSON(const std::string& path)
{
    unsigned char* rawBytes = nullptr;
    int cleanedSize = 0;
    std::vector<char> cleaned;
    {
    cute_tiled_map_t* map = cute_tiled_load_map_from_file(path.c_str(), nullptr);
    if (!map) {
        // NOTE: if this identifier doesn't compile, open cute_tiled.h and
        // search for "error" to find the real error-string symbol/function.
        TraceLog(LOG_ERROR, "TileMap: cute_tiled failed to parse '%s'. Reason: %s",
                 path.c_str(), cute_tiled_error_reason ? cute_tiled_error_reason : "(unknown)");
        return false;
    }

    width  = map->width;
    height = map->height;

    layers.clear();
    triggers.clear(); // Reset triggers on load
    musicGroup.clear(); // Reset so a map without the property doesn't inherit the old one

    // 1. Load ALL tile layers into separate vectors in sequential order.
    // This walks recursively so that layers organized inside a Tiled
    // "group" layer (a folder in the editor UI) are still picked up -
    // cute_tiled only exposes those through layer->layers, not through
    // the top-level map->layers list, so a flat loop silently drops them.
    std::function<void(cute_tiled_layer_t*)> processLayers =
        [&](cute_tiled_layer_t* first)
    {
        for (cute_tiled_layer_t* layer = first; layer; layer = layer->next)
        {
            TraceLog(LOG_INFO, "TileMap: found layer '%s' (type='%s')",
                      layer->name.ptr ? layer->name.ptr : "(unnamed)", layer->type.ptr);

            // 1. Process Tile Layers
            if (strcmp(layer->type.ptr, "tilelayer") == 0)
            {
                std::vector<int> layerData(width * height, 0);
                for (int y = 0; y < height; y++)
                {
                    for (int x = 0; x < width; x++)
                    {
                        int gid = layer->data[y * width + x];
                        gid = cute_tiled_unset_flags(gid);
                        layerData[y * width + x] = gid;
                    }
                }
                layers.push_back(layerData);
            }
            // 2. Process Object/Trigger Layers
            else if (strcmp(layer->type.ptr, "objectgroup") == 0)
            {
                for (cute_tiled_object_t* obj = layer->objects; obj; obj = obj->next)
                {
                    MapTrigger trig;
                    trig.name = obj->name.ptr ? obj->name.ptr : "";
                    trig.type = obj->type.ptr ? obj->type.ptr : "";
                    trig.id = obj->id;
                    TraceLog(LOG_INFO, "cute_tiled raw object: id=%d name.ptr=%p type.ptr=%p type='%s'",
                              obj->id, (void*)obj->name.ptr, (void*)obj->type.ptr,
                              obj->type.ptr ? obj->type.ptr : "(null)");
                    trig.bounds = Rectangle{
                        (float)obj->x,
                        (float)obj->y,
                        (float)(obj->width > 0 ? obj->width : TILE_SIZE),
                        (float)(obj->height > 0 ? obj->height : TILE_SIZE)
                    };

                    // Read custom properties defined in Tiled
                    for (int p = 0; p < obj->property_count; p++)
                    {
                        auto& prop = obj->properties[p];

                        if (strcmp(prop.name.ptr, "targetMap") == 0)
                            trig.targetMap = prop.data.string.ptr;
                        else if (strcmp(prop.name.ptr, "targetX") == 0)
                            trig.targetPos.x = static_cast<float>(prop.data.integer * TILE_SIZE); // or pixels directly
                        else if (strcmp(prop.name.ptr, "targetY") == 0)
                            trig.targetPos.y = static_cast<float>(prop.data.integer * TILE_SIZE);
                        else if (strcmp(prop.name.ptr, "eventId") == 0)
                            trig.eventId = prop.data.string.ptr;
                        else if (strcmp(prop.name.ptr, "enemyId") == 0)
                            trig.enemyId = prop.data.string.ptr; // for type "battle"
                        else if (strcmp(prop.name.ptr, "triggerOnce") == 0)
                            trig.triggerOnce = prop.data.boolean;
                    }

                    triggers.push_back(trig);
                }
            }
            // 3. Recurse into Group Layers (folders in the Tiled editor)
            else if (strcmp(layer->type.ptr, "group") == 0)
            {
                processLayers(layer->layers);
            }
        }
    };
    processLayers(map->layers);

    TraceLog(LOG_INFO, "TileMap: loaded %d tile layer(s), %d trigger(s)",
              static_cast<int>(layers.size()), static_cast<int>(triggers.size()));

    // 2. Determine max GID
    int maxGID = 0;
    for (cute_tiled_tileset_t* ts = map->tilesets; ts; ts = ts->next)
        maxGID = std::max(maxGID, ts->firstgid + ts->tilecount);

    tileDefs.clear();
    tileDefs.resize(maxGID + 1);

    // 3. Load tileset textures + properties
    for (cute_tiled_tileset_t* ts = map->tilesets; ts; ts = ts->next)
    {
        std::string imagePath = ts->image.ptr ? ts->image.ptr : "";
        int columns = ts->columns;
        int count   = ts->tilecount;

        auto cached = tilesetCache.find(imagePath);
        if (cached != tilesetCache.end() &&
            static_cast<int>(cached->second.tileTextures.size()) >= count)
        {
            // Already decoded + uploaded from an earlier map load (e.g. this
            // is the map we just warped away from, or two maps share a
            // tileset). Reuse the existing GPU textures instead of redoing
            // the PNG decode and per-tile LoadTextureFromImage upload.
            for (int i = 0; i < count; i++)
            {
                int gid = ts->firstgid + i;
                if (gid >= static_cast<int>(tileDefs.size()))
                    tileDefs.resize(gid + 1);

                tileDefs[gid].texture = cached->second.tileTextures[i];
                tileDefs[gid].color = GREEN;
                tileDefs[gid].solid = false;
                tileDefs[gid].overhang = false;
                tileDefs[gid].visible = true;
                tileDefs[gid].visualHeight = 0;
            }
        }
        else
        {
            Image img = LoadImage(imagePath.c_str());
            if (!img.data) {
                TraceLog(LOG_ERROR, "TileMap: failed to load tileset image '%s' (cwd: %s) - "
                                     "tiles from this set will draw as blank rectangles.",
                         imagePath.c_str(), GetWorkingDirectory());
            }
            else
            {
                CachedTileset newEntry;
                newEntry.tileTextures.resize(count);

                for (int i = 0; i < count; i++)
                {
                    int gid = ts->firstgid + i;

                    // Defensive: don't trust maxGID blindly. Some tilesets
                    // (margin/spacing quirks, image-collection tilesets) can
                    // report a tilecount that doesn't match the true tile
                    // range, which previously caused tileDefs[gid] to run past
                    // the end of the vector and crash on the debug assertion.
                    if (gid >= static_cast<int>(tileDefs.size()))
                        tileDefs.resize(gid + 1);

                    int x = (i % columns) * ts->tilewidth;
                    int y = (i / columns) * ts->tileheight;

                    if (y + ts->tileheight > img.height)
                        continue; // prevent out-of-bounds slicing

                    Image tileImg = ImageFromImage(img, Rectangle{
                        (float)x, (float)y,
                        (float)ts->tilewidth, (float)ts->tileheight
                    });

                    Texture2D tex = LoadTextureFromImage(tileImg);
                    UnloadImage(tileImg);

                    newEntry.tileTextures[i] = tex;

                    tileDefs[gid].texture = tex;
                    tileDefs[gid].color = GREEN;
                    tileDefs[gid].solid = false;
                    tileDefs[gid].overhang = false;
                    tileDefs[gid].visible = true;
                    tileDefs[gid].visualHeight = 0;
                }

                UnloadImage(img);
                tilesetCache[imagePath] = std::move(newEntry);
            }
        }

        // Apply tile properties
       // Add sound reading inside LoadFromTiledJSON property parsing loop:
for (cute_tiled_tile_descriptor_t* desc = ts->tiles; desc; desc = desc->next)
{
    int gid = desc->tile_index + ts->firstgid;

    // Same defensive growth as above - a <tile> descriptor's tile_index
    // is not guaranteed to stay under ts->tilecount for every tileset
    // layout, and this loop runs even when the tileset image failed to
    // load (img.data == nullptr), so tileDefs may not have been grown yet.
    if (gid >= static_cast<int>(tileDefs.size()))
        tileDefs.resize(gid + 1);

    for (int p = 0; p < desc->property_count; p++)
    {
        auto& prop = desc->properties[p];
        if (strcmp(prop.name.ptr, "solid") == 0)
            tileDefs[gid].solid = prop.data.boolean;

        if (strcmp(prop.name.ptr, "overhang") == 0)
            tileDefs[gid].overhang = prop.data.boolean;

        if (strcmp(prop.name.ptr, "visible") == 0)
            tileDefs[gid].visible = prop.data.boolean;

        if (strcmp(prop.name.ptr, "visualHeight") == 0 || strcmp(prop.name.ptr, "visualheight") == 0)
            tileDefs[gid].visualHeight = prop.data.integer;

        // Parse custom footstep string property from Tiled
        if (strcmp(prop.name.ptr, "footstep") == 0)
            tileDefs[gid].footstepType = prop.data.string.ptr;

        // Random-encounter properties (see RollEncounterAtPixel)
        if (strcmp(prop.name.ptr, "encounterRate") == 0)
            tileDefs[gid].encounterRate = prop.data.integer;
        if (strcmp(prop.name.ptr, "encounterEnemy") == 0)
            tileDefs[gid].encounterEnemy = prop.data.string.ptr;
    }
}
}

    // Read MAP-level properties (not object/tile properties). This must live
    // outside the tileset/tile-descriptor loops above - it previously only
    // ran when a tileset happened to have tiles with custom properties,
    // which meant a map could silently keep the *previous* map's musicGroup.
    for (int p = 0; p < map->property_count; p++)
    {
        auto& prop = map->properties[p];
        if (strcmp(prop.name.ptr, "musicGroup") == 0)
            musicGroup = prop.data.string.ptr;
    }

    cute_tiled_free_map(map);
    return true;
}
}
std::vector<MapTrigger*> TileMap::GetCollidingTriggers(Rectangle playerBounds)
{
    std::vector<MapTrigger*> colliding;
    for (auto& trig : triggers)
    {
        if (trig.triggerOnce && trig.hasFired) continue;

        if (CheckCollisionRecs(playerBounds, trig.bounds))
        {
            colliding.push_back(&trig);
        }
    }
    return colliding;
}
void TileMap::MarkTriggerFired(int triggerId)
{
    for (auto& trig : triggers)
    {
        if (trig.id == triggerId)
            trig.hasFired = true;
    }
}

// ---------------- Audio Methods ----------------

void TileMap::RegisterFootstepSound(const std::string& type, const std::string& soundFilePath)
{
    Sound s = LoadSound(soundFilePath.c_str());
    if (s.stream.buffer != nullptr)
    {
        footstepSounds[type] = s;
    }
}

void TileMap::UnloadAudio()
{
    for (auto& pair : footstepSounds)
    {
        UnloadSound(pair.second);
    }
    footstepSounds.clear();
}

std::string TileMap::GetFootstepTypeAtPixel(Vector2 pixelPos) const
{
    int tx = static_cast<int>(pixelPos.x) / TILE_SIZE;
    int ty = static_cast<int>(pixelPos.y) / TILE_SIZE;

    if (tx < 0 || ty < 0 || tx >= width || ty >= height)
        return "";

    // Iterate top-down through layers to find the highest layer with a footstep sound
    for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i)
    {
        int id = layers[i][ty * width + tx];
        if (id > 0 && id < static_cast<int>(tileDefs.size()))
        {
            if (!tileDefs[id].footstepType.empty())
            {
                return tileDefs[id].footstepType;
            }
        }
    }
    return "";
}

bool TileMap::RollEncounterAtPixel(Vector2 pixelPos, std::string& outEnemy) const
{
    int tx = static_cast<int>(pixelPos.x) / TILE_SIZE;
    int ty = static_cast<int>(pixelPos.y) / TILE_SIZE;
    if (pixelPos.x < 0 || pixelPos.y < 0 || tx >= width || ty >= height)
        return false;

    // Top-most layer that defines an encounter wins.
    for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i)
    {
        int id = layers[i][ty * width + tx];
        if (id > 0 && id < static_cast<int>(tileDefs.size()) && tileDefs[id].encounterRate > 0)
        {
            if (GetRandomValue(1, 100) <= tileDefs[id].encounterRate)
            {
                outEnemy = tileDefs[id].encounterEnemy;
                return true;
            }
            return false;
        }
    }
    return false;
}

void TileMap::PlayFootstepAtPixel(Vector2 pixelPos) const
{
    std::string type = GetFootstepTypeAtPixel(pixelPos);
    if (type.empty()) return;

    auto it = footstepSounds.find(type);
    if (it != footstepSounds.end())
    {
        // Add a tiny random pitch variance so footsteps sound natural
        float pitch = 0.95f + (GetRandomValue(0, 10) / 100.0f);
        SetSoundPitch(it->second, pitch);
        
        PlaySound(it->second);
    }
}
/*

}
*/
bool TileMap::LoadTilesetImage(const std::string &imagePath, int tileWidth, int tileHeight, int firstGID)
{
    Image img = LoadImage(imagePath.c_str());
    if (!img.data)
        return false;

    int columns = img.width / tileWidth;
    int rows = img.height / tileHeight;
    int count = columns * rows;

    if (tileDefs.size() < static_cast<size_t>(firstGID + count))
        tileDefs.resize(firstGID + count);

    for (int i = 0; i < count; i++)
    {
        int x = (i % columns) * tileWidth;
        int y = (i / columns) * tileHeight;

        Image tileImg = ImageFromImage(img, Rectangle{(float)x, (float)y, (float)tileWidth, (float)tileHeight});
        tileDefs[firstGID + i].texture = LoadTextureFromImage(tileImg);
        UnloadImage(tileImg);

        tileDefs[firstGID + i].color = WHITE;
    }

    UnloadImage(img);
    return true;
}

void TileMap::LoadPlaceholder(int w, int h)
{
    width = w;
    height = h;

    layers.clear();
    layers.push_back(std::vector<int>(width * height, 0));
    auto& tiles = layers[0];

    tileDefs.clear();
    tileDefs.resize(4);

    tileDefs[0] = { Texture2D{}, MAGENTA,   false, false, 0.0f };
    tileDefs[1] = { Texture2D{}, GREEN,     false, false, 0.0f };
    tileDefs[2] = { Texture2D{}, DARKGRAY,  true,  false, 0.0f };
    tileDefs[3] = { Texture2D{}, DARKGREEN, false, true,  16.0f };

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            bool border = (x == 0 || y == 0 || x == width - 1 || y == height - 1);
            tiles[y * width + x] = border ? 2 : 1;
        }
    }

    if (width > 10 && height > 10)
    {
        tiles[5 * width + 5] = 2;
        tiles[5 * width + 6] = 2;
        tiles[6 * width + 5] = 2;
    }

    if (width > 15 && height > 12)
    {
        tiles[10 * width + 8] = 3;
        tiles[10 * width + 9] = 3;
        tiles[10 * width + 10] = 3;
    }
}

int TileMap::TileAt(int layerIdx, int x, int y) const
{
    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size()))
        return -1;
    if (x < 0 || y < 0 || x >= width || y >= height)
        return -1;
    return layers[layerIdx][y * width + x];
}

void TileMap::Draw() const
{
    for (const auto& layer : layers)
    {
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                int id = layer[y * width + x];
                if (id <= 0 || id >= static_cast<int>(tileDefs.size()))
                    continue;

                Rectangle rect{
                    static_cast<float>(x * TILE_SIZE),
                    static_cast<float>(y * TILE_SIZE),
                    static_cast<float>(TILE_SIZE),
                    static_cast<float>(TILE_SIZE)};
                DrawRectangleRec(rect, tileDefs[id].color);
                DrawRectangleLinesEx(rect, 1.0f, Fade(BLACK, 0.15f));
            }
        }
    }
}

void TileMap::DrawGround() const
{
    for (const auto& layer : layers)
    {
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                int id = layer[y * width + x];
                if (id <= 0 || id >= static_cast<int>(tileDefs.size()))
                    continue;

                const TileDef &def = tileDefs[id];
                if (!def.visible || def.overhang)
                    continue;

                if (def.texture.id != 0)
                {
                    DrawTexture(def.texture, x * TILE_SIZE, y * TILE_SIZE, WHITE);
                }
                else
                {
                    DrawRectangle(x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE, def.color);
                }
            }
        }
    }
}

void TileMap::DrawOverhang() const
{
    for (const auto& layer : layers)
    {
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                int id = layer[y * width + x];
                if (id <= 0 || id >= static_cast<int>(tileDefs.size()))
                    continue;

                const TileDef &def = tileDefs[id];
                if (!def.visible || !def.overhang)
                    continue;

                float drawY = y * TILE_SIZE - def.visualHeight;

                if (def.texture.id != 0)
                {
                    DrawTexture(def.texture, x * TILE_SIZE, drawY, WHITE);
                }
                else
                {
                    DrawRectangle(x * TILE_SIZE, drawY, TILE_SIZE, TILE_SIZE + def.visualHeight, def.color);
                }
            }
        }
    }
}

bool TileMap::IsSolidAtPixel(Vector2 pixelPos) const
{
    int tx = static_cast<int>(pixelPos.x) / TILE_SIZE;
    int ty = static_cast<int>(pixelPos.y) / TILE_SIZE;

    if (tx < 0 || ty < 0 || tx >= width || ty >= height)
        return true; // Out-of-bounds is solid

    // Check across all layers: if any layer contains a solid tile at this position, block movement
    for (const auto& layer : layers)
    {
        int id = layer[ty * width + tx];
        if (id > 0 && id < static_cast<int>(tileDefs.size()))
        {
            if (tileDefs[id].solid)
                return true;
        }
    }

    return false;
}