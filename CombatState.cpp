#include "CombatState.hpp"
#include "StateManager.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

// ------------------------------------------------------------------
// Tunables / small helpers
// ------------------------------------------------------------------

static char* kHeroName   = "HERO";
static const int   kPotionHeal = 15;
static const float kTextSpeed  = 45.0f; // characters per second
static const float kHpRollRate = 25.0f; // HP points per second on the rolling counter
static const int   kFontSize   = 26;

static bool ConfirmPressed() {
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_SPACE);
}
static bool CancelPressed() {
    return IsKeyPressed(KEY_X) || IsKeyPressed(KEY_BACKSPACE);
}
static bool UpPressed() {
    return IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
}
static bool DownPressed() {
    return IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
}

// Total EXP you need to have to reach (level + 1).
static int ExpNeeded(int level) { return level * level * 8; }

static std::string WrapText(const std::string& text, int maxWidth, int fontSize) {
    std::istringstream iss(text);
    std::string word, line, out;
    while (iss >> word) {
        std::string test = line.empty() ? word : line + " " + word;
        if (!line.empty() && MeasureText(test.c_str(), fontSize) > maxWidth) {
            out += line + "\n";
            line = word;
        } else {
            line = test;
        }
    }
    out += line;
    return out;
}

// Earthbound-ish window: black box, white double border.
static void DrawWindow(Rectangle r) {
    DrawRectangleRec(r, BLACK);
    DrawRectangleLinesEx(r, 4.0f, RAYWHITE);
    Rectangle inner{ r.x + 6, r.y + 6, r.width - 12, r.height - 12 };
    DrawRectangleLinesEx(inner, 2.0f, GRAY);
}

// ------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------

CombatState::CombatState(Player* p, const std::string& enemyId, FinishCallback cb)
    : player(p), enemy(), onFinish(std::move(cb)), displayHp(p->hp)
{
    //Default Enemy table. These ids are what you put in the "enemyId" property of
    // a Tiled object with type "battle". Add your own enemies here.
    //                      name              hp  atk def exp  color        canFlee
    if (enemyId == "dog")
        enemy = EnemyDef{ "Runaway Dog",    18, 7,  1,  8,  ORANGE,      true  };
    else if (enemyId == "snake")
        enemy = EnemyDef{ "Coil Snake",     10, 5,  0,  5,  LIME,        true  };
    else if (enemyId == "ant")
        enemy = EnemyDef{ "Titanic Ant",    60, 11, 3,  40, MAROON,      false };
    else // "crow" and anything unknown
        enemy = EnemyDef{ "Spiteful Crow",  14, 6,  1,  6,  DARKPURPLE,  true  };

    enemyHp = enemy.maxHp;
    Say("The " + enemy.name + " draws near!");
}

// ------------------------------------------------------------------
// Message queue (typewriter text box)
// ------------------------------------------------------------------

void CombatState::Say(const std::string& text, std::function<void()> onShow) {
    queue.push_back(Message{ text, std::move(onShow) });
}

void CombatState::UpdateMessages(float dt) {
    if (!hasCurrent) {
        if (queue.empty()) {
            // Don't move on until the rolling HP counter has caught up.
            if (displayHp != player->hp) return;

            auto done = std::move(onQueueDone);
            onQueueDone = nullptr;
            if (done) done();          // may queue more messages / finish the battle
            else phase = Phase::Menu;  // default: back to the command menu
            return;
        }

        current = std::move(queue.front());
        queue.pop_front();
        hasCurrent = true;
        typed = 0.0f;
        holdTimer = 0.0f;
        int boxW = static_cast<int>(GetScreenWidth() * 0.8f) - 40;
        wrapped = WrapText(current.text, boxW, kFontSize);
        if (current.onShow) current.onShow();
        return;
    }

    const float total = static_cast<float>(wrapped.size());
    if (typed < total) {
        typed += kTextSpeed * dt;
        if (ConfirmPressed()) typed = total; // skip to end of line
    } else {
        holdTimer += dt;
        // Auto-advance like Earthbound's battle text, or press to speed it up.
        if (ConfirmPressed() || holdTimer > 0.7f) {
            hasCurrent = false;
            typed = total;
        }
    }
}

// ------------------------------------------------------------------
// Update
// ------------------------------------------------------------------

void CombatState::Update(float dt) {
    if (finished) return;

    time += dt;
    enemyFlash = std::max(0.0f, enemyFlash - dt);
    hurtFlash  = std::max(0.0f, hurtFlash - dt * 2.0f);
    if (enemyHp <= 0) enemyAlpha = std::max(0.0f, enemyAlpha - dt * 1.5f);

    // Rolling HP counter: ticks toward the real value like an odometer.
    if (displayHp != player->hp) {
        rollAccum += dt * kHpRollRate;
        while (rollAccum >= 1.0f && displayHp != player->hp) {
            displayHp += (displayHp < player->hp) ? 1 : -1;
            rollAccum -= 1.0f;
        }
    } else {
        rollAccum = 0.0f;
    }

    switch (phase) {
        case Phase::Messages: UpdateMessages(dt); break;
        case Phase::Menu:     UpdateMenu();       break;
        case Phase::Goods:    UpdateGoods();      break;
    }
}

void CombatState::UpdateMenu() {
    if (UpPressed())   cursor = (cursor + 3) % 4;
    if (DownPressed()) cursor = (cursor + 1) % 4;

    if (ConfirmPressed()) {
        switch (cursor) {
            case 0: DoBash();  break;
            case 1: goodsCursor = 0; phase = Phase::Goods; break;
            case 2: DoDefend(); break;
            case 3: DoRun();   break;
        }
    }
}

void CombatState::UpdateGoods() {
    if (UpPressed() || DownPressed()) goodsCursor = 1 - goodsCursor;
    if (CancelPressed()) { phase = Phase::Menu; return; }

    if (ConfirmPressed()) {
        if (goodsCursor == 0) DoPotion();
        else phase = Phase::Menu;
    }
}

// ------------------------------------------------------------------
// Player actions
// ------------------------------------------------------------------

void CombatState::DoBash() {
    bool miss  = GetRandomValue(1, 100) <= 6;
    bool smash = !miss && GetRandomValue(1, 16) == 1; // Earthbound's "SMASH!!" crit
    int dmg = std::max(1, player->offense - enemy.defense + GetRandomValue(-1, 2));
    if (smash) dmg *= 3;

    Say(std::string(kHeroName) + " attacks!");
    if (miss) {
        Say("Just missed!");
    } else {
        std::string line = smash ? "SMASH!! " : "";
        line += "The " + enemy.name + " took " + std::to_string(dmg) + " points of damage!";
        Say(line, [this, dmg]() {
            enemyHp = std::max(0, enemyHp - dmg);
            enemyFlash = 0.3f;
        });
    }
    ThenDo([this]() { AfterPlayerAction(); });
}

void CombatState::DoDefend() {
    Say(std::string(kHeroName) + " braces for impact.", [this]() { defending = true; });
    ThenDo([this]() { StartEnemyTurn(); });
}

void CombatState::DoRun() {
    if (!enemy.canFlee) {
        Say("There's no escaping this battle!");
        ThenDo([this]() { StartEnemyTurn(); });
    } else if (GetRandomValue(1, 100) <= 60) {
        Say(std::string(kHeroName) + " ran away!");
        ThenDo([this]() { Finish(false); });
    } else {
        Say(std::string("But ") + kHeroName + " couldn't get away!");
        ThenDo([this]() { StartEnemyTurn(); });
    }
}

void CombatState::DoPotion() {
    if (player->potions <= 0) {
        Say(std::string(kHeroName) + " has no Potions left!");
        phase = Phase::Messages; // no onQueueDone -> returns to the menu
        return;
    }
    player->potions--;
    int heal = std::min(kPotionHeal, player->maxHp - player->hp);
    Say(std::string(kHeroName) + " drinks a Potion.");
    Say("Recovered " + std::to_string(heal) + " HP!", [this, heal]() { player->hp += heal; });
    ThenDo([this]() { StartEnemyTurn(); });
}

// ------------------------------------------------------------------
// Turn flow
// ------------------------------------------------------------------

void CombatState::AfterPlayerAction() {
    if (enemyHp <= 0) Win();
    else StartEnemyTurn();
}

void CombatState::StartEnemyTurn() {
    bool miss = GetRandomValue(1, 100) <= 8;
    int dmg = std::max(1, enemy.attack - player->defense + GetRandomValue(-1, 2));
    if (defending) dmg = std::max(1, dmg / 2);

    Say("The " + enemy.name + " attacks!");
    if (miss) {
        Say(std::string(kHeroName) + " dodged the attack!");
    } else {
        Say(std::string(kHeroName) + " took " + std::to_string(dmg) + " points of damage!",
            [this, dmg]() {
                player->hp = std::max(0, player->hp - dmg);
                hurtFlash = 0.5f;
            });
    }
    ThenDo([this]() {
        defending = false;
        AfterEnemyAction();
    });
}

void CombatState::AfterEnemyAction() {
    if (player->hp <= 0) Lose();
    else phase = Phase::Menu;
}

void CombatState::LevelUp() {
    player->level++;
    player->maxHp += 6;
    player->hp += 6;
    player->offense += 1;
    if (player->level % 2 == 0) player->defense += 1;
}

void CombatState::Win() {
    int gain = enemy.exp;
    int startLevel = player->level;
    int lv = startLevel;
    int ups = 0;
    while (player->exp + gain >= ExpNeeded(lv)) { lv++; ups++; }

    Say(std::string(kHeroName) + " won the battle!");
    Say("Gained " + std::to_string(gain) + " EXP points.", [this, gain, ups]() {
        player->exp += gain;
        for (int i = 0; i < ups; i++) LevelUp();
    });
    for (int i = 0; i < ups; i++)
        Say(std::string(kHeroName) + " reached level " + std::to_string(startLevel + i + 1) + "!");

    ThenDo([this]() { Finish(true); });
}

void CombatState::Lose() {
    Say(std::string(kHeroName) + " was defeated...");
    Say("...but gets back up, good as new.");
    ThenDo([this]() {
        // Placeholder defeat handling: heal up and return to the overworld.
        // Swap in a game-over state / respawn here when you have one.
        player->hp = player->maxHp;
        Finish(false);
    });
}

void CombatState::Finish(bool won) {
    if (finished) return;
    finished = true;
    if (onFinish) onFinish(won);
    if (manager) manager->Pop(); // deferred by StateManager until Update returns
}

// ------------------------------------------------------------------
// Drawing
// ------------------------------------------------------------------

void CombatState::DrawBackground() const {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int band = 6;

    // Wobbling psychedelic stripes, in the spirit of Earthbound's battle backgrounds.
    for (int y = 0; y < sh; y += band) {
        float wave = std::sin(time * 1.5f + y * 0.04f) * 30.0f;
        float hue = std::fmod(time * 20.0f + y * 0.35f, 360.0f);
        Color a = ColorFromHSV(hue, 0.55f, 0.45f);
        Color b = ColorFromHSV(std::fmod(hue + 60.0f, 360.0f), 0.55f, 0.25f);
        DrawRectangle(0, y, sw, band, b);
        for (int x = -64 + static_cast<int>(wave); x < sw; x += 128)
            DrawRectangle(x, y, 64, band, a);
    }
}

void CombatState::DrawEnemy() const {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    if (enemyAlpha <= 0.0f) return;

    float shake = (enemyFlash > 0.0f) ? static_cast<float>(GetRandomValue(-5, 5)) : 0.0f;
    float bob = std::sin(time * 2.0f) * 4.0f; // idle bobbing
    const float size = 180.0f;
    Rectangle body{ sw / 2.0f - size / 2.0f + shake, sh * 0.45f - size / 2.0f + bob, size, size };

    Color col = (enemyFlash > 0.0f) ? WHITE : enemy.color;
    DrawRectangleRec(body, Fade(col, enemyAlpha));
    DrawRectangleLinesEx(body, 4.0f, Fade(BLACK, enemyAlpha));
    // Placeholder face - replace with a texture when you have enemy art.
    DrawCircle(static_cast<int>(body.x + 55), static_cast<int>(body.y + 70), 12, Fade(WHITE, enemyAlpha));
    DrawCircle(static_cast<int>(body.x + 125), static_cast<int>(body.y + 70), 12, Fade(WHITE, enemyAlpha));
    DrawCircle(static_cast<int>(body.x + 55), static_cast<int>(body.y + 70), 5, Fade(BLACK, enemyAlpha));
    DrawCircle(static_cast<int>(body.x + 125), static_cast<int>(body.y + 70), 5, Fade(BLACK, enemyAlpha));

    int nameW = MeasureText(enemy.name.c_str(), 24);
    DrawText(enemy.name.c_str(), static_cast<int>(sw / 2.0f - nameW / 2.0f),
             static_cast<int>(body.y + size + 14), 24, Fade(WHITE, enemyAlpha));
}

void CombatState::Draw() {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();

    DrawBackground();
    DrawEnemy();
    if (hurtFlash > 0.0f) DrawRectangle(0, 0, sw, sh, Fade(RED, hurtFlash * 0.5f));

    // --- message window (top) ---
    Rectangle msg{ sw * 0.1f, 24.0f, sw * 0.8f, 130.0f };
    DrawWindow(msg);

    std::string text;
    if (phase == Phase::Menu) {
        text = std::string("What will ") + kHeroName + " do?";
    } else if (phase == Phase::Goods) {
        text = "Use which item?";
    } else {
        int n = std::min(static_cast<int>(wrapped.size()), static_cast<int>(typed));
        text = wrapped.substr(0, n);
    }
    DrawText(text.c_str(), static_cast<int>(msg.x + 20), static_cast<int>(msg.y + 18), kFontSize, WHITE);

    // --- command menu / goods list (under the message window, left) ---
    if (phase == Phase::Menu) {
        static const char* items[4] = { "Bash", "Goods", "Defend", "Run Away" };
        Rectangle cmd{ msg.x, msg.y + msg.height + 10.0f, 230.0f, 4 * 36.0f + 24.0f };
        DrawWindow(cmd);
        for (int i = 0; i < 4; i++) {
            std::string label = (i == cursor ? "> " : "  ") + std::string(items[i]);
            DrawText(label.c_str(), static_cast<int>(cmd.x + 20),
                     static_cast<int>(cmd.y + 14 + i * 36), kFontSize, WHITE);
        }
    } else if (phase == Phase::Goods) {
        Rectangle box{ msg.x, msg.y + msg.height + 10.0f, 280.0f, 2 * 36.0f + 24.0f };
        DrawWindow(box);
        std::string potion = std::string(goodsCursor == 0 ? "> " : "  ") +
                             "Potion x" + std::to_string(player->potions);
        std::string back = std::string(goodsCursor == 1 ? "> " : "  ") + "Back";
        DrawText(potion.c_str(), static_cast<int>(box.x + 20), static_cast<int>(box.y + 14), kFontSize, WHITE);
        DrawText(back.c_str(), static_cast<int>(box.x + 20), static_cast<int>(box.y + 14 + 36), kFontSize, WHITE);
    }

    // --- HP window (bottom center) ---
    Rectangle hpBox{ (sw - 340.0f) / 2.0f, sh - 120.0f, 340.0f, 90.0f };
    DrawWindow(hpBox);
    Color hpColor = (displayHp <= player->maxHp / 5) ? RED : WHITE; // Earthbound-style low-HP warning
    DrawText(kHeroName, static_cast<int>(hpBox.x + 20), static_cast<int>(hpBox.y + 12), 24, WHITE);
    DrawText(TextFormat("Lv %d", player->level), static_cast<int>(hpBox.x + hpBox.width - 90),
             static_cast<int>(hpBox.y + 12), 24, WHITE);
    DrawText(TextFormat("HP %03d / %03d", displayHp, player->maxHp), static_cast<int>(hpBox.x + 20),
             static_cast<int>(hpBox.y + 46), 28, hpColor);
}
