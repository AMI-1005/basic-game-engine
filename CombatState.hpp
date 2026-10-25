#pragma once
#include <deque>
#include <functional>
#include <string>
#include "raylib.h"
#include "GameState.hpp"
#include "Player.hpp"

// Earthbound-style, turn-based battle. Pushed on top of the OverworldState
// (see the "battle" trigger handling there) and pops itself when it ends.
//
// Flow: text box types out messages -> command menu (Bash / Goods / Defend /
// Run Away) -> your action plays out as messages -> enemy action plays out
// -> back to the menu, until someone drops to 0 HP or you escape.
class CombatState : public GameState {
public:
    // onFinish(won) is called once, right before the state pops itself.
    using FinishCallback = std::function<void(bool won)>;

    CombatState(Player* player, const std::string& enemyId, FinishCallback onFinish = nullptr);
    ~CombatState() override = default;

    void Update(float dt) override;
    void Draw() override;

private:
    struct EnemyDef {
        std::string name;
        int maxHp;
        int attack;
        int defense;
        int exp;
        Color color;
        bool canFlee;
    };

    struct Message {
        std::string text;
        std::function<void()> onShow; // runs the moment the line starts (apply damage, etc.)
    };

    enum class Phase { Messages, Menu, Goods };

    // --- data ---
    Player* player;
    EnemyDef enemy;
    int enemyHp;
    FinishCallback onFinish;

    // --- flow ---
    Phase phase = Phase::Messages;
    std::deque<Message> queue;
    std::function<void()> onQueueDone; // what to do when the queue drains (default: show menu)
    bool hasCurrent = false;
    Message current;
    std::string wrapped;    // current line, word-wrapped to the text box
    float typed = 0.0f;     // characters revealed so far
    float holdTimer = 0.0f; // pause after a line finishes typing before auto-advancing
    int cursor = 0;
    int goodsCursor = 0;
    bool defending = false;
    bool finished = false;

    // --- visuals ---
    float time = 0.0f;
    int displayHp;          // Earthbound "rolling" HP counter
    float rollAccum = 0.0f;
    float enemyFlash = 0.0f;
    float enemyAlpha = 1.0f;
    float hurtFlash = 0.0f;

    // --- helpers ---
    void Say(const std::string& text, std::function<void()> onShow = nullptr);
    void ThenDo(std::function<void()> fn) { onQueueDone = std::move(fn); phase = Phase::Messages; }

    void UpdateMessages(float dt);
    void UpdateMenu();
    void UpdateGoods();

    void DoBash();
    void DoDefend();
    void DoRun();
    void DoPotion();

    void StartEnemyTurn();
    void AfterPlayerAction();
    void AfterEnemyAction();
    void Win();
    void Lose();
    void Finish(bool won);
    void LevelUp();

    void DrawBackground() const;
    void DrawEnemy() const;
};
