#include "TestCharacter.hpp"
#include "Manager/MapManger.hpp"

namespace maptool
{
	TestCharacter::TestCharacter(const MapManger& maps) : cell_(maps.entryPoint()) {}
}
