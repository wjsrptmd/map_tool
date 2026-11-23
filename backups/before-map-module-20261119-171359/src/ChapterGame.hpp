#pragma once
#include "ChapterData.hpp"
#include <filesystem>
#include <functional>
#include <optional>

namespace chapter
{
struct Progress
{
    int stage = 0, battleMask = 0, herbs = 0, map = 0;
    unsigned flags = 0;
    sf::Vector2i cell{2, 7};
    std::array<bool, 10> recruited{};
    std::vector<int> party;
};
bool saveProgress(const std::filesystem::path& path, const Progress& progress);
std::optional<Progress> loadProgress(const std::filesystem::path& path);

struct Actor
{
    int type = -1;
    std::string name;
    sf::Vector2i cell;
    bool enemy = false, moved = false, acted = false;
    int hp = 1, maxHp = 1, mp = 0, maxMp = 0;
    int attack = 1, defense = 0, movement = 4, range = 1;
    int shield = 0, poison = 0, rooted = 0, weakened = 0, boosted = 0;
    sf::Color color;
    bool alive() const { return hp > 0; }
};

enum class Scene { Title, Explore, Roster, Journal, Battle, Result, Ending };
enum class Command { Move, Attack, Skill, Guard, Potion };
struct Button
{
    sf::FloatRect rect;
    std::string label;
    std::function<void()> action;
    bool enabled = true, selected = false;
};
struct Dialog
{
    std::string title;
    std::vector<std::string> pages;
    std::size_t page = 0;
    std::string accept = "확인";
    std::function<void()> action;
    bool choice = false;
};

class Game
{
public:
    explicit Game(std::filesystem::path savePath);
    void loadFont();
    void update(float dt);
    void click(sf::Vector2f point);
    void key(sf::Keyboard::Key code);
    void render(sf::RenderTarget& target, sf::Vector2f mouse = {-1, -1});
    void newGame();
    void startBattle(int id);
    void preparePreview(const std::string& kind);
    void runTests(const std::filesystem::path& scratch);
    bool closeRequested = false;
    sf::Font font;
    bool fontReady = false;

private:
    std::filesystem::path savePath;
    Progress progress;
    Scene scene = Scene::Title;
    Scene returnScene = Scene::Explore;
    std::vector<Button> buttons;
    std::optional<Dialog> dialog;
    std::string notice;
    float elapsed = 0, enemyTimer = 0;
    bool grid = true, enemyTurn = false, victory = false;
    bool previewMode = false;
    std::array<std::array<char, MapW>, MapH> terrain{};
    std::vector<sf::Vector2i> walkRoute;
    std::size_t walkStep = 0;
    sf::Vector2f heroPosition;
    int pendingEvent = -1;
    std::array<std::array<char, BattleW>, BattleH> field{};
    std::vector<Actor> actors;
    int selected = 0, battleId = 0, round = 1, enemyCursor = 0, potions = 3;
    Command command = Command::Move;
    std::vector<std::string> log;

    void buildRegion();
    bool worldWalkable(sf::Vector2i cell) const;
    std::vector<sf::Vector2i> worldPath(sf::Vector2i from, sf::Vector2i to) const;
    bool eventVisible(const WorldEvent& event) const;
    void interact(int eventIndex);
    bool canRecruit(int id) const;
    void recruit(int id);
    void travel(int region);
    void showDialog(std::string title, std::vector<std::string> pages,
                    std::function<void()> action = {}, std::string accept = "확인", bool choice = false);
    void advanceDialog();
    void checkpoint();
    void restore();
    void toggleParty(int id);
    int level() const;
    Actor makeAlly(int type, sf::Vector2i position) const;
    bool openCell(sf::Vector2i cell, int ignore = -1) const;
    std::vector<sf::Vector2i> battlePath(sf::Vector2i from, sf::Vector2i to, int actor) const;
    bool sight(sf::Vector2i from, sf::Vector2i to) const;
    bool inRange(const Actor& actor, sf::Vector2i target, int range) const;
    int actorAt(sf::Vector2i cell) const;
    void moveActor(int who, sf::Vector2i target);
    bool attack(int who, int target);
    bool useSkill(int who, sf::Vector2i target);
    bool drinkPotion(int who);
    void guard(int who);
    void damage(int target, int amount);
    void heal(int target, int amount);
    void record(const std::string& message);
    void endPlayerTurn();
    void enemyAction(int who);
    void beginPlayerTurn();
    void checkBattleEnd();
    void finishBattle();
    void autoPlayerTurn();
    void battleClick(sf::Vector2f point);
    void addButton(sf::RenderTarget& out, sf::FloatRect rect, std::string label,
                   std::function<void()> action, bool enabled = true, bool active = false);
    void drawWorld(sf::RenderTarget& out, sf::Vector2f mouse);
    void drawBattle(sf::RenderTarget& out, sf::Vector2f mouse);
    void drawRoster(sf::RenderTarget& out);
    void drawJournal(sf::RenderTarget& out);
    void drawDialog(sf::RenderTarget& out);
    void drawTitle(sf::RenderTarget& out);
    void drawEnding(sf::RenderTarget& out);
    void drawResult(sf::RenderTarget& out);
    void label(sf::RenderTarget& out, const std::string& value, sf::Vector2f at,
               unsigned size = 18, sf::Color color = {225, 231, 213}) const;
    float wrapped(sf::RenderTarget& out, const std::string& value, sf::Vector2f at,
                  float width, unsigned size = 18, sf::Color color = {225, 231, 213}) const;
};
}
