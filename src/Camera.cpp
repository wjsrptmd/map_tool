#include "Camera.hpp"
#include "manager/MapManger.hpp"
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

		float axisOrigin(float character, int mapSize, int viewSize)
		{
			const float half = static_cast<float>(viewSize / 2);
			if (mapSize >= viewSize)
				return std::max(0.f, std::min(character - half, static_cast<float>(mapSize - viewSize)));
			return static_cast<float>((mapSize - viewSize) / 2);
		}
	}

	Cell Camera::focus(const MapManger& maps, Cell character) const
	{
		return { axisFocus(character.x, maps.current().width, maps.viewColumns()),
				axisFocus(character.y, maps.current().height, maps.viewRows()) };
	}

	Vec2 Camera::viewOrigin(const MapManger& maps, float x, float y) const
	{
		return { axisOrigin(x, maps.current().width, maps.viewColumns()),
			axisOrigin(y, maps.current().height, maps.viewRows()) };
	}
}
