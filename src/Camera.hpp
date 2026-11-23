#pragma once
#include "Manager/MapManger.hpp"

namespace maptool
{
// Chooses the view focus. The character cell itself stays with TestCharacter.
class Camera
{
public:
    Cell focus(const MapManger& maps, Cell character) const;
};
}
