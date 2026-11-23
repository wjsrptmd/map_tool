#include <SFML/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int Columns = 14, Rows = 9;
constexpr float Tile = 64.f;
constexpr unsigned Width = 976, Height = 760;
constexpr sf::Vector2f Origin{40.f, 112.f};
constexpr sf::Vector2i Start{1, 4};
// . grass, : footpath, T tree, R rock, ~ water. Coordinates are column, row.
constexpr std::array<const char*, Rows> Map{
    "TT..T....TT.TT",
    "T...T..R.....T",
    "..R....T..~~..",
    "...T......~~..",
    "::::::..T....T",
    ".T..T:::...R..",
    "......T:::::..",
    "T..R......T::.",
    "TT...T..T...TT"
};

bool inside(sf::Vector2i p)
{
    return p.x >= 0 && p.y >= 0 && p.x < Columns && p.y < Rows;
}
bool walkable(sf::Vector2i p)
{
    return inside(p) && (Map[p.y][p.x] == '.' || Map[p.y][p.x] == ':');
}
int index(sf::Vector2i p) { return p.y * Columns + p.x; }
sf::Vector2f center(sf::Vector2i p)
{
    return Origin + sf::Vector2f{(p.x + .5f) * Tile, (p.y + .5f) * Tile};
}
std::optional<sf::Vector2i> cellAt(sf::Vector2f p)
{
    p -= Origin;
    if (p.x < 0 || p.y < 0 || p.x >= Columns * Tile || p.y >= Rows * Tile)
        return std::nullopt;
    return sf::Vector2i{static_cast<int>(p.x / Tile), static_cast<int>(p.y / Tile)};
}

// Breadth-first search: all steps cost one; diagonals are intentionally disabled.
std::vector<sf::Vector2i> findPath(sf::Vector2i from, sf::Vector2i to)
{
    if (!walkable(from) || !walkable(to) || from == to) return {};
    std::array<int, Columns * Rows> parent;
    parent.fill(-1);
    std::queue<sf::Vector2i> frontier;
    frontier.push(from);
    parent[index(from)] = index(from);
    constexpr std::array<sf::Vector2i, 4> directions{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    while (!frontier.empty() && parent[index(to)] == -1)
    {
        const auto p = frontier.front();
        frontier.pop();
        for (const auto d : directions)
        {
            const auto next = p + d;
            if (!walkable(next) || parent[index(next)] != -1) continue;
            parent[index(next)] = index(p);
            frontier.push(next);
        }
    }
    if (parent[index(to)] == -1) return {};
    std::vector<sf::Vector2i> result;
    for (int at = index(to); at != index(from); at = parent[at])
        result.push_back({at % Columns, at / Columns});
    std::reverse(result.begin(), result.end());
    return result;
}

struct Traveler
{
    sf::Vector2i cell = Start;
    sf::Vector2f position = center(Start);
    std::vector<sf::Vector2i> route;
    std::size_t next = 0;
    int facing = 1;
    bool moving() const { return next < route.size(); }
    void reset() { *this = Traveler{}; }
    void update(float dt)
    {
        float distance = 210.f * dt;
        while (moving())
        {
            const auto target = center(route[next]);
            const auto delta = target - position;
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (delta.x != 0) facing = delta.x > 0 ? 1 : -1;
            if (length > distance)
            {
                position += delta * (distance / length);
                break;
            }
            position = target;
            cell = route[next++];
            distance -= length;
        }
    }
};

void box(sf::RenderTarget& out, sf::Vector2f p, sf::Vector2f size, sf::Color color)
{
    sf::RectangleShape shape(size);
    shape.setPosition(p);
    shape.setFillColor(color);
    out.draw(shape);
}
void ellipse(sf::RenderTarget& out, sf::Vector2f p, sf::Vector2f radius, sf::Color color)
{
    sf::CircleShape shape(1.f, 28);
    shape.setScale(radius);
    shape.setOrigin({1.f, 1.f});
    shape.setPosition(p);
    shape.setFillColor(color);
    out.draw(shape);
}
void text(sf::RenderTarget& out, const sf::Font* font, const std::string& value,
          sf::Vector2f p, unsigned size, sf::Color color)
{
    if (!font) return;
    sf::Text label(*font, value, size);
    label.setPosition(p);
    label.setFillColor(color);
    out.draw(label);
}

void drawTree(sf::RenderTarget& out, sf::Vector2f p, int seed)
{
    ellipse(out, p + sf::Vector2f{3, 15}, {27, 11}, {17, 37, 31, 140});
    box(out, p + sf::Vector2f{-5, -8}, {11, 30}, {99, 68, 42});
    box(out, p + sf::Vector2f{-2, -6}, {3, 25}, {142, 101, 55});
    ellipse(out, p + sf::Vector2f{-13, -12}, {22, 21}, {29, 80, 56});
    ellipse(out, p + sf::Vector2f{14, -14}, {23, 23}, {33, 94, 61});
    ellipse(out, p + sf::Vector2f{0, -29}, {27, 24}, {42, 110, 68});
    ellipse(out, p + sf::Vector2f{-7, -35}, {17, 13}, {64, 132, 77});
    ellipse(out, p + sf::Vector2f{11, -25}, {9, 7}, {77, 143, 80});
    if (seed % 3 == 0)
        ellipse(out, p + sf::Vector2f{-18, -15}, {3, 3}, {207, 151, 77});
}
void drawTraveler(sf::RenderTarget& out, const Traveler& unit, float time)
{
    auto p = unit.position;
    ellipse(out, p + sf::Vector2f{0, 19}, {19, 8}, {16, 34, 29, 150});
    const float stride = unit.moving() ? std::sin(time * 15.f) * 3.f : 0.f;
    box(out, p + sf::Vector2f{-10, 8 + stride}, {8, 13}, {49, 44, 43});
    box(out, p + sf::Vector2f{3, 8 - stride}, {8, 13}, {49, 44, 43});
    p.y -= std::abs(stride) * .5f;
    sf::ConvexShape cape(4);
    cape.setPoint(0, p + sf::Vector2f{-10, -16});
    cape.setPoint(1, p + sf::Vector2f{9, -16});
    cape.setPoint(2, p + sf::Vector2f{17, 12});
    cape.setPoint(3, p + sf::Vector2f{-17, 12});
    cape.setFillColor({44, 102, 111});
    out.draw(cape);
    box(out, p + sf::Vector2f{-6, -8}, {12, 20}, {81, 153, 151});
    box(out, p + sf::Vector2f{-9, 5}, {19, 4}, {110, 75, 47});
    ellipse(out, p + sf::Vector2f{0, -22}, {14, 15}, {35, 81, 87});
    ellipse(out, p + sf::Vector2f{static_cast<float>(unit.facing * 2), -20}, {9, 10}, {239, 198, 145});
    ellipse(out, p + sf::Vector2f{static_cast<float>(unit.facing * 6), -21}, {1.5f, 2}, {40, 47, 47});
    box(out, p + sf::Vector2f{-11, -35}, {21, 7}, {44, 102, 111});
    box(out, p + sf::Vector2f{18, -19}, {3, 40}, {164, 122, 69});
    ellipse(out, p + sf::Vector2f{19.5f, -22}, {4, 5}, {151, 215, 195});
}

void render(sf::RenderTarget& out, const Traveler& unit, const sf::Font* font,
            std::optional<sf::Vector2i> hover, bool grid, float time,
            const std::string& status)
{
    out.clear({19, 33, 32});
    text(out, font, "WHISPERING WOODS", {40, 20}, 28, {226, 232, 207});
    text(out, font, "FOREST EXPLORATION  /  movement prototype", {42, 59}, 15, {131, 163, 144});
    box(out, Origin - sf::Vector2f{4, 4}, {Columns * Tile + 8, Rows * Tile + 8}, {70, 90, 67});
    for (int y = 0; y < Rows; ++y)
        for (int x = 0; x < Columns; ++x)
        {
            const sf::Vector2i cell{x, y};
            const auto p = center(cell);
            const int seed = (x * 37 + y * 19) % 7;
            const char terrain = Map[y][x];
            sf::Color ground{static_cast<std::uint8_t>(58 + seed * 2),
                             static_cast<std::uint8_t>(89 + seed * 2), 58};
            if (terrain == ':') ground = {134, 119, 80};
            if (terrain == '~') ground = {44, 103, 115};
            box(out, p - sf::Vector2f{32, 32}, {64, 64}, ground);
            if (grid)
            {
                box(out, p - sf::Vector2f{32, 32}, {64, 1}, {170, 193, 153, 30});
                box(out, p - sf::Vector2f{32, 32}, {1, 64}, {170, 193, 153, 30});
            }
            for (int n = 0; n < 4; ++n)
            {
                const sf::Vector2f speck{static_cast<float>((seed * 13 + n * 17) % 52 - 26),
                                       static_cast<float>((seed * 7 + n * 11) % 48 - 24)};
                if (terrain == '~') box(out, p + speck, {12, 2}, {87, 151, 154, 140});
                else box(out, p + speck, {2, 4}, terrain == ':'
                         ? sf::Color{162, 146, 103, 170} : sf::Color{100, 132, 73, 160});
            }
            if (terrain == '.' && seed == 3)
            {
                ellipse(out, p + sf::Vector2f{16, 13}, {2, 2}, {232, 197, 109});
                ellipse(out, p + sf::Vector2f{21, 8}, {2, 2}, {223, 225, 179});
            }
        }
    auto route = unit.moving() ? std::vector<sf::Vector2i>(unit.route.begin() + unit.next, unit.route.end())
                              : (hover ? findPath(unit.cell, *hover) : std::vector<sf::Vector2i>{});
    for (const auto cell : route)
        ellipse(out, center(cell), {4, 4}, {174, 224, 195, 200});
    if (hover)
    {
        sf::RectangleShape outline({60, 60});
        outline.setPosition(center(*hover) - sf::Vector2f{30, 30});
        outline.setFillColor(sf::Color::Transparent);
        outline.setOutlineThickness(2.f);
        outline.setOutlineColor(walkable(*hover) ? sf::Color{205, 226, 167} : sf::Color{220, 110, 98});
        out.draw(outline);
    }
    const int actorRow = std::clamp(static_cast<int>((unit.position.y - Origin.y) / Tile), 0, Rows - 1);
    for (int y = 0; y < Rows; ++y)
    {
        for (int x = 0; x < Columns; ++x)
        {
            const auto p = center({x, y});
            if (Map[y][x] == 'T') drawTree(out, p, x + y);
            else if (Map[y][x] == 'R')
            {
                ellipse(out, p + sf::Vector2f{2, 12}, {25, 10}, {22, 43, 33, 130});
                ellipse(out, p, {23, 16}, {100, 115, 105});
                ellipse(out, p + sf::Vector2f{-5, -6}, {16, 9}, {142, 151, 131});
                ellipse(out, p + sf::Vector2f{11, 9}, {8, 4}, {70, 104, 63});
            }
        }
        if (y == actorRow) drawTraveler(out, unit, time);
    }
    box(out, {40, 707}, {896, 1}, {70, 91, 76});
    text(out, font, "LMB  Move     RMB / R  Reset     G  Grid     ESC  Exit", {40, 722}, 15, {190, 207, 183});
    text(out, font, unit.moving() ? "Walking..." : status, {620, 724}, 13, {145, 186, 163});
}

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void selfTest()
{
    for (const auto row : Map) check(std::string(row).size() == Columns, "Map width mismatch");
    check(!cellAt(Origin - sf::Vector2f{1, 1}), "Outside coordinates accepted");
    check(findPath(Start, {0, 0}).empty(), "Tree destination accepted");
    check(findPath(Start, {10, 2}).empty(), "Water destination accepted");
    const auto path = findPath(Start, {12, 7});
    check(!path.empty(), "Destination unreachable");
    auto previous = Start;
    for (const auto cell : path)
    {
        check(walkable(cell), "Route enters obstacle");
        check(std::abs(cell.x - previous.x) + std::abs(cell.y - previous.y) == 1, "Invalid route step");
        previous = cell;
    }
    Traveler unit;
    unit.route = path;
    for (int i = 0; i < 2000; ++i) unit.update(1.f / 60.f);
    check(unit.cell == sf::Vector2i{12, 7} && !unit.moving(), "Movement did not finish");
    check(unit.position == center({12, 7}), "Movement missed tile center");
    unit.reset();
    check(unit.cell == Start && !unit.moving(), "Reset failed");
    std::cout << "PASS: map, bounds, blocked destinations, route steps, movement, reset\n";
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc > 1 && std::string(argv[1]) == "--self-test")
        {
            selfTest();
            return 0;
        }
        sf::Font font;
        const bool hasFont = font.openFromFile("C:/Windows/Fonts/segoeui.ttf");
        if (argc > 2 && std::string(argv[1]) == "--preview")
        {
            sf::RenderTexture image({Width, Height});
            render(image, Traveler{}, hasFont ? &font : nullptr, sf::Vector2i{12, 7}, true, 0,
                   "Choose a clearing to walk to.");
            image.display();
            return image.getTexture().copyToImage().saveToFile(argv[2]) ? 0 : 1;
        }
        sf::RenderWindow window(sf::VideoMode({Width, Height}),
            "Whispering Woods | Left click: walk | Right click: reset | G: grid | Esc: exit",
            sf::Style::Titlebar | sf::Style::Close);
        window.setFramerateLimit(60);
        Traveler unit;
        bool grid = true;
        float time = 0;
        std::string status = "Choose a clearing to walk to.";
        sf::Clock clock;
        while (window.isOpen())
        {
            const float dt = std::min(clock.restart().asSeconds(), .1f);
            time += dt;
            while (const auto event = window.pollEvent())
            {
                if (event->is<sf::Event::Closed>()) window.close();
                else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
                {
                    if (key->code == sf::Keyboard::Key::Escape) window.close();
                    if (key->code == sf::Keyboard::Key::G) grid = !grid;
                    if (key->code == sf::Keyboard::Key::R) { unit.reset(); status = "Back at the forest entrance."; }
                }
                else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
                {
                    if (mouse->button == sf::Mouse::Button::Right)
                    { unit.reset(); status = "Back at the forest entrance."; }
                    else if (mouse->button == sf::Mouse::Button::Left && !unit.moving())
                    {
                        const auto cell = cellAt(window.mapPixelToCoords(mouse->position));
                        if (cell && *cell != unit.cell)
                        {
                            unit.route = findPath(unit.cell, *cell);
                            unit.next = 0;
                            status = unit.route.empty() ? "That tile is blocked." : "Arrived. Choose another tile.";
                        }
                    }
                }
            }
            if (!window.isOpen()) break;
            unit.update(dt);
            render(window, unit, hasFont ? &font : nullptr,
                   cellAt(window.mapPixelToCoords(sf::Mouse::getPosition(window))), grid, time, status);
            window.display();
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
