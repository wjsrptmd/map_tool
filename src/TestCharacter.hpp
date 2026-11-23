#pragma once
#include <SFML/System/Vector2.hpp>

namespace maptool
{
class MapManger;

// The only character. Placed on the current map's entry tile.
class TestCharacter
{
public:
    explicit TestCharacter(const MapManger& maps);

    sf::Vector2i cell() const noexcept { return cell_; }
    void moveTo(sf::Vector2i cell) noexcept { cell_ = cell; }

private:
    sf::Vector2i cell_;
};
}
