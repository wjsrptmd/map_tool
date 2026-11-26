#pragma once
#include "Camera.hpp"
#include "service/CharacterService.hpp"
#include "manager/MapManger.hpp"
#include <SFML/Graphics.hpp>
#include <map>
#include <string>

namespace maptool
{
// Values read from src/game_config.json.
struct GameSettings
{
    std::string project;
    bool debugMode = false;
    Cell viewShift;
    int chapter = 1;
    Cell startMap;
    std::string characterId;
    float speed = 0.f;
};

// Window, input, and drawing. Map data stays in MapManger.
class Game
{
public:
    Game();
    int run(const GameSettings& settings, bool selfTest);

private:
    class TilePictures
    {
    public:
        const sf::Texture* get(const MapManger& maps, const std::string& id);

    private:
        std::map<std::string, sf::Texture> loaded_;
    };

    static sf::Color toSf(Color color);
    static Cell directionStep(sf::Keyboard::Key key);
    const sf::Texture& spriteTexture(const Animation& animation);
    void drawTile(sf::RenderTarget& out, const sf::Texture* texture, sf::Vector2f position) const;
    void drawGame(sf::RenderTarget& out);
    void drawLoading(sf::RenderTarget& out) const;
    void openChapter(const GameSettings& settings);
    bool checkPlacements(const GameSettings& settings);
    bool checkNpcMovement();
    bool checkPlayerGlide(int actorId, Cell start);
    bool selfTest(const GameSettings& settings);
    int play(const GameSettings& settings);

    MapManger maps_;
    CharacterManager characters_;
    CharacterService service_;
    Camera camera_;
    TilePictures pictures_;
    std::map<std::string, sf::Texture> sprites_;
};
}
