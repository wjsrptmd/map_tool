#pragma once
#include "Camera.hpp"
#include "TestCharacter.hpp"
#include "Manager/MapManger.hpp"
#include <SFML/Graphics.hpp>
#include <map>
#include <memory>
#include <string>

namespace maptool
{
// Values read from src/self_test.json and src/game_config.json.
struct GameSettings
{
    std::string project;
    Cell viewShift;
    int chapter = 1;
    Cell startMap;
    float characterRadiusScale = 0.f;
    Color characterColor;
};

// Window, input, and drawing. Map data stays in MapManger.
class Game
{
public:
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
    void drawTile(sf::RenderTarget& out, const sf::Texture* texture, sf::Vector2f position) const;
    void drawMap(sf::RenderTarget& out, const GameSettings& settings);
    void drawLoading(sf::RenderTarget& out) const;
    bool moveCharacter(Cell step);
    void openChapter(int chapter);
    bool selfTest(const GameSettings& settings);
    int play(const GameSettings& settings);

    MapManger maps_;
    std::unique_ptr<TestCharacter> character_;
    Camera camera_;
    TilePictures pictures_;
};
}
