#pragma once
#include "raylib.h"

// Base class for anything that lives in the world and needs
// per-frame updating + drawing: player, NPCs, enemies, items...
class Entity {
public:
    virtual ~Entity() = default;
    virtual void Update(float dt) = 0;
    virtual void Draw() const = 0;

    Vector2 position{ 0, 0 };
    Rectangle GetCollider() const {
        return { position.x, position.y, size.x, size.y };
    }

    Vector2 size{ 24, 24 };
};
