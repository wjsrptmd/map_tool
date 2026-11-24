#include "Character.hpp"
#include <algorithm>
#include <stdexcept>

namespace maptool
{
int animationFrame(const Animation& animation, float elapsed)
{
    if (animation.frames <= 1 || animation.fps <= 0.f || elapsed <= 0.f) return 0;
    const int index = static_cast<int>(elapsed * animation.fps);
    if (!animation.loop) return std::min(index, animation.frames - 1);
    return index % animation.frames;
}

const Animation& Character::animation(const std::string& pose, const std::string& facing) const
{
    const auto states = animations.find(pose);
    if (states == animations.end())
        throw std::runtime_error("missing animation " + pose + "/" + facing);
    const auto found = states->second.find(facing);
    if (found == states->second.end())
        throw std::runtime_error("missing animation " + pose + "/" + facing);
    return found->second;
}

void Character::pose(const std::string& nextState, const std::string& facing)
{
    animation(nextState, facing);
    if (state == nextState && direction == facing) return;
    state = nextState;
    direction = facing;
    elapsed = 0.f;
}

void Character::walk(const std::string& facing)
{
    pose("walk", facing);
}

void Character::idle(const std::string& facing)
{
    pose("idle", facing);
}

void Character::advance(float seconds)
{
    if (seconds > 0.f) elapsed += seconds;
}

int Character::frame() const
{
    return animationFrame(animation(state, direction), elapsed);
}
}
