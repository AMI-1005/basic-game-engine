#pragma once
#include "Entity.hpp"
#include "TileMap.hpp"

class Player : public Entity {
public:
    explicit Player(const TileMap* map) : tileMap(map) {}

    void Update(float dt) override;
    void Draw() const override;

    int hp = 20;
    int maxHp = 20;
    int level = 1;
    int exp = 0;
    int offense = 6;   // combat: base attack power
    int defense = 2;   // combat: damage reduction
    int potions = 3;   // combat: "Goods" item, heals 15 HP
    bool stepped = false; // set true each footstep; OverworldState reads + clears it
    float stepTimer = 0.0f;
    float stepInterval = 0.35f;
private:
    // Moves on one axis at a time and resolves collision against the
    // tilemap by checking the corners of the entity's collider.
    void MoveAxis(float dx, float dy);
    bool CollidesAt(Vector2 candidatePos) const;
    const TileMap* tileMap;
    float speed = 120.0f; // pixels per second
    Vector2 facing{ 0, 1 };
};
