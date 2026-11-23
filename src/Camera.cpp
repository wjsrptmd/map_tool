#include "Camera.hpp"
#include "Manager/MapManger.hpp"
#include <algorithm>

namespace maptool
{
namespace
{
int axisFocus(int character, int mapSize, int viewSize)
{
    const int origin = mapSize >= viewSize
        ? std::max(0, std::min(character - viewSize / 2, mapSize - viewSize))
        : (mapSize - viewSize) / 2;
    return origin + viewSize / 2;
}
}

sf::Vector2i Camera::focus(const MapManger& maps, sf::Vector2i character) const
{
    return {axisFocus(character.x, maps.current().width, maps.viewColumns()),
            axisFocus(character.y, maps.current().height, maps.viewRows())};
}
}
