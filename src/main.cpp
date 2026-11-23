#include "Camera.hpp"
#include "TestCharacter.hpp"
#include "Manager/MapFile.hpp"
#include "Manager/MapManger.hpp"
#include <nlohmann/json.hpp>
#include <SFML/Graphics.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace
{
sf::Color toSf(maptool::Color color)
{
    return {color.r, color.g, color.b};
}

class TilePictures
{
public:
    const sf::Texture* get(const maptool::MapManger& maps, const std::string& id)
    {
        if (id.empty()) return nullptr;
        const auto found = loaded_.find(id);
        if (found != loaded_.end()) return &found->second;
        sf::Texture texture(maps.tile(id).image);
        texture.setSmooth(true);
        return &loaded_.try_emplace(id, std::move(texture)).first->second;
    }

private:
    std::map<std::string, sf::Texture> loaded_;
};

void drawTile(sf::RenderTarget& out, const maptool::GlobalConfig& config, const sf::Texture* texture, sf::Vector2f position)
{
    const float tile = config.tileSize;
    if (texture == nullptr || texture->getSize().x == 0 || texture->getSize().y == 0)
    {
        sf::RectangleShape empty({tile, tile});
        empty.setPosition(position);
        empty.setFillColor(toSf(config.emptyTile));
        out.draw(empty);
        return;
    }
    sf::Sprite sprite(*texture);
    const auto size = texture->getSize();
    sprite.setScale({tile / static_cast<float>(size.x), tile / static_cast<float>(size.y)});
    sprite.setPosition(position);
    out.draw(sprite);
}

void drawMap(sf::RenderTarget& out, const maptool::MapManger& maps, const maptool::TestCharacter& character, TilePictures& pictures)
{
    const auto& config = maps.config();
    const auto& view = maps.view();
    const sf::Vector2f margin{config.margin.x, config.margin.y};
    for (int y = 0; y < maps.viewRows(); ++y)
        for (int x = 0; x < maps.viewColumns(); ++x)
        {
            const auto& id = view.tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
            const auto position = margin + sf::Vector2f{x * config.tileSize, y * config.tileSize};
            drawTile(out, config, pictures.get(maps, id), position);
        }
    const auto local = character.cell() - view.origin;
    constexpr float characterRadius = 12.f;
    sf::CircleShape body(characterRadius);
    body.setOrigin({characterRadius, characterRadius});
    body.setPosition(margin + sf::Vector2f{(local.x + .5f) * config.tileSize, (local.y + .5f) * config.tileSize});
    body.setFillColor({214, 176, 132});
    out.draw(body);
}

int jsonInt(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_number_integer())
        throw std::runtime_error(std::string(key) + " must be an integer");
    return object[key].get<int>();
}

std::string jsonText(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_string() || object[key].get<std::string>().empty())
        throw std::runtime_error(std::string(key) + " must be text");
    return object[key].get<std::string>();
}

maptool::Cell directionStep(sf::Keyboard::Key key)
{
    switch (key)
    {
    case sf::Keyboard::Key::Up: return {0, -1};
    case sf::Keyboard::Key::Down: return {0, 1};
    case sf::Keyboard::Key::Left: return {-1, 0};
    case sf::Keyboard::Key::Right: return {1, 0};
    default: return {};
    }
}

bool moveCharacter(maptool::MapManger& maps, maptool::Camera& camera, maptool::TestCharacter& character, maptool::Cell step)
{
    if (step == maptool::Cell{}) return false;
    const auto next = character.cell() + step;
    if (maps.walkable(next))
        character.moveTo(next);
    else if (const auto arrived = maps.enterNeighbor(next))
        character.moveTo(*arrived);
    else
        return false;
    maps.follow(camera.focus(maps, character.cell()));
    return true;
}

void requireKeys(const nlohmann::json& object, std::initializer_list<const char*> keys)
{
    if (!object.is_object()) throw std::runtime_error("needs an object");
    for (const auto& item : object.items())
    {
        bool known = false;
        for (const char* key : keys)
            if (item.key() == key) known = true;
        if (!known) throw std::runtime_error("unknown key " + item.key());
    }
    for (const char* key : keys)
        if (!object.contains(key)) throw std::runtime_error(std::string(key) + " is required");
}

bool selfTest(maptool::MapManger& maps, const maptool::TestCharacter& character)
{
    std::filesystem::path file;
    for (auto dir = maptool::projectDirectory();; dir = dir.parent_path())
    {
        const auto candidate = dir / "src" / "self_test.json";
        if (std::filesystem::is_regular_file(candidate))
        {
            file = candidate;
            break;
        }
        if (dir.parent_path() == dir) throw std::runtime_error("src/self_test.json not found");
    }
    std::ifstream in(file);
    if (!in) throw std::runtime_error("failed to open " + file.generic_string());
    const auto root = nlohmann::json::parse(in);
    requireKeys(root, {"project", "viewShift", "startMap"});
    if (maps.projectName() != jsonText(root, "project")) return false;
    if (!maps.walkable(character.cell())) return false;

    maps.follow(character.cell());
    if (static_cast<int>(maps.view().tiles.size()) != maps.viewRows()
        || static_cast<int>(maps.view().tiles.front().size()) != maps.viewColumns())
        return false;
    requireKeys(root["viewShift"], {"x", "y"});
    const maptool::Cell shift{jsonInt(root["viewShift"], "x"), jsonInt(root["viewShift"], "y")};
    const auto origin = maps.view().origin;
    maps.follow(character.cell() + shift);
    if (maps.view().origin != origin + shift) return false;
    const auto center = maps.view().tiles[static_cast<std::size_t>(maps.viewRows() / 2)][static_cast<std::size_t>(maps.viewColumns() / 2)];
    if (center != maps.tileAt(character.cell().x + shift.x, character.cell().y + shift.y)) return false;
    maps.follow(character.cell());

    requireKeys(root["startMap"], {"x", "y"});
    return maps.currentMapX() == jsonInt(root["startMap"], "x") && maps.currentMapY() == jsonInt(root["startMap"], "y");
}
}

int main(int argc, char** argv)
{
    try
    {
        maptool::MapManger maps;
        maptool::TestCharacter character(maps);
        maptool::Camera camera;
        if (argc > 1 && std::string(argv[1]) == "--self-test")
            return selfTest(maps, character) ? 0 : 1;
        maps.follow(camera.focus(maps, character.cell()));

        const auto& config = maps.config();
        TilePictures pictures;
        const auto width = static_cast<unsigned>(maps.viewColumns() * config.tileSize + config.margin.x * 2);
        const auto height = static_cast<unsigned>(maps.viewRows() * config.tileSize + config.margin.y * 2);
        const std::string title = config.windowTitle + " - " + maps.projectName();
        sf::RenderWindow window(sf::VideoMode({width, height}), sf::String::fromUtf8(title.begin(), title.end()));
        window.setFramerateLimit(static_cast<unsigned>(config.framerate));
        constexpr sf::Keyboard::Key directions[] = {
            sf::Keyboard::Key::Up, sf::Keyboard::Key::Down, sf::Keyboard::Key::Left, sf::Keyboard::Key::Right};
        std::array<bool, 4> held{};
        int moving = -1;
        sf::Clock repeat;
        const sf::Time repeatInterval = sf::milliseconds(110);
        while (window.isOpen())
        {
            while (const auto event = window.pollEvent())
            {
                if (event->is<sf::Event::Closed>()) window.close();
                else if (event->is<sf::Event::FocusLost>())
                {
                    held.fill(false);
                    moving = -1;
                }
                else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        if (key->code != directions[i] || held[static_cast<std::size_t>(i)]) continue;
                        held[static_cast<std::size_t>(i)] = true;
                        moving = i;
                        moveCharacter(maps, camera, character, directionStep(key->code));
                        repeat.restart();
                    }
                }
                else if (const auto* key = event->getIf<sf::Event::KeyReleased>())
                {
                    for (int i = 0; i < 4; ++i)
                        if (key->code == directions[i]) held[static_cast<std::size_t>(i)] = false;
                    if (moving >= 0 && !held[static_cast<std::size_t>(moving)])
                    {
                        moving = -1;
                        for (int i = 0; i < 4; ++i)
                            if (held[static_cast<std::size_t>(i)]) moving = i;
                        repeat.restart();
                    }
                }
            }
            if (moving >= 0 && repeat.getElapsedTime() >= repeatInterval)
            {
                moveCharacter(maps, camera, character, directionStep(directions[moving]));
                repeat.restart();
            }
            window.clear(toSf(config.background));
            drawMap(window, maps, character, pictures);
            window.display();
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
