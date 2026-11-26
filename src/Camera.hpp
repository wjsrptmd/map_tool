#pragma once
#include "manager/MapManger.hpp"

namespace maptool
{
// Chooses the view focus from a character cell supplied by the caller.
class Camera
{
public:
    Cell focus(const MapManger& maps, Cell character) const;
    Vec2 viewOrigin(const MapManger& maps, float x, float y) const;
};
}
