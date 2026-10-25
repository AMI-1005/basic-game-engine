#pragma once

class StateManager; // forward declaration - set by StateManager::Push

// Base interface for all game states (overworld, battle, dialogue, menu...).
// Push a concrete implementation onto StateManager to activate it.
class GameState {
public:
    virtual ~GameState() = default;
    virtual void Update(float dt) = 0;
    virtual void Draw() = 0;

    // Filled in by StateManager::Push. A state uses this to push other
    // states on top of itself (overworld -> combat) or pop itself off
    // (combat -> back to overworld).
    StateManager* manager = nullptr;
};
