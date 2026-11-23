#pragma once
#include <SFML/System/Vector2.hpp>

namespace maptool
{
class MapManger;

// Chooses the view focus. The character cell itself stays with TestCharacter.
class Camera
{
public:
    sf::Vector2i focus(const MapManger& maps, sf::Vector2i character) const;
};
}
