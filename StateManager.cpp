#include "StateManager.hpp"
#include "GameState.hpp"

void StateManager::Apply(Command& cmd) {
    if (cmd.isPush) {
        states.push_back(std::move(cmd.state));
    } else if (!states.empty()) {
        states.pop_back();
    }
}

void StateManager::Push(std::unique_ptr<GameState> state) {
    state->manager = this;
    Command cmd{ true, std::move(state) };
    if (updating) pending.push_back(std::move(cmd));
    else Apply(cmd);
}

void StateManager::Pop() {
    Command cmd{ false, nullptr };
    if (updating) pending.push_back(std::move(cmd));
    else Apply(cmd);
}

void StateManager::Update(float dt) {
    // Only the topmost state receives updates (e.g. a paused
    // overworld sits underneath an active dialogue box / battle).
    if (!states.empty()) {
        updating = true;
        states.back()->Update(dt);
        updating = false;
    }

    // Apply whatever the state asked for during its Update.
    for (auto& cmd : pending) Apply(cmd);
    pending.clear();
}

void StateManager::Draw() {
    for (auto& s : states) s->Draw();
}
