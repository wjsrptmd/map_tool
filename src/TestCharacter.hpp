#pragma once
#include "Manager/MapManger.hpp"

namespace maptool
{
// The only character. Placed on the current map's entry tile.
class TestCharacter
{
public:
    explicit TestCharacter(const MapManger& maps);

    Cell cell() const noexcept { return cell_; }
    void moveTo(Cell cell) noexcept { cell_ = cell; }

private:
    Cell cell_;
};
}
