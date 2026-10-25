#pragma once
#include <memory>
#include <vector>

// Forward declaration - full definition lives in GameState.hpp
class GameState;

// A stack of game states. The state on top receives Update().
// Draw() renders every state in the stack, bottom to top, so a
// DialogueState pushed on top of OverworldState can render as an
// overlay without the overworld disappearing underneath it.
//
// Push()/Pop() are safe to call from inside a state's Update(): while an
// update is running they are queued and applied right after it returns,
// so a state can never be destroyed while its own Update() is on the stack.
class StateManager {
public:
    void Push(std::unique_ptr<GameState> state);
    void Pop();
    void Update(float dt);
    void Draw();
    bool Empty() const { return states.empty(); }

private:
    struct Command {
        bool isPush;
        std::unique_ptr<GameState> state; // only used for pushes
    };

    void Apply(Command& cmd);

    std::vector<std::unique_ptr<GameState>> states;
    std::vector<Command> pending;
    bool updating = false;
};
