#include "Player.hpp"
#include "raylib.h"
#include "raymath.h"
#include "OverworldState.hpp"

bool Player::CollidesAt(Vector2 candidatePos) const {
    // Check all 4 corners of the collider at the candidate position.
    Vector2 corners[4] = {
        { candidatePos.x, candidatePos.y },
        { candidatePos.x + size.x - 1, candidatePos.y },
        { candidatePos.x, candidatePos.y + size.y - 1 },
        { candidatePos.x + size.x - 1, candidatePos.y + size.y - 1 },
    };
    for (const auto& c : corners) {
        if (tileMap->IsSolidAtPixel(c)) return true;
    }
    return false;
}

void Player::MoveAxis(float dx, float dy) {
    Vector2 candidate = { position.x + dx, position.y + dy };
    if (!CollidesAt(candidate)) {
        position = candidate;
    }
}

void Player::Update(float dt) {
    Vector2 dir{ 0, 0 };
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) dir.x += 1;
    if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) dir.x -= 1;
    if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) dir.y += 1;
    if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) dir.y -= 1;

    if (dir.x != 0 || dir.y != 0) {
        // Normalize so diagonal movement isn't faster than cardinal movement.
        float len = Vector2Length(dir);
        dir.x /= len;
        dir.y /= len;
        facing = dir;
    }

    // Move on each axis separately so sliding along walls works correctly
    MoveAxis(dir.x * speed * dt, 0);
    MoveAxis(0, dir.y * speed * dt);

    bool isMoving = (dir.x != 0 || dir.y != 0);

    if (isMoving)
    {
        stepTimer += dt;
        if (stepTimer >= stepInterval)
        {
            stepTimer = 0.0f;
            stepped = true; // tells OverworldState a footstep just happened (encounter roll)

            // Get player's foot position (bottom-center of bounding box)
            Vector2 feetPos = {
                position.x + size.x / 2.0f,
                position.y + size.y
            };

            // FIX: Use member tileMap directly
            if (tileMap) {
                tileMap->PlayFootstepAtPixel(feetPos);
            }
        }
    }
    else
    {
        // Reset timer so the step sound plays immediately when starting to walk again
        stepTimer = stepInterval; 
    }
}

void Player::Draw() const {
    // Placeholder rendering: a colored rectangle with a small facing indicator.
    Rectangle body = GetCollider();
    DrawRectangleRec(body, BLUE);
    DrawRectangleLinesEx(body, 2.0f, DARKBLUE);

    Vector2 center = { position.x + size.x / 2, position.y + size.y / 2 };
    Vector2 facingDot = { center.x + facing.x * 14, center.y + facing.y * 14 };
    DrawCircleV(facingDot, 3.0f, YELLOW);
}