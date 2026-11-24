#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace maptool
{
struct CharacterAnchor
{
    float x = 0.f;
    float y = 0.f;
};

struct Animation
{
    std::filesystem::path image;
    int frames = 0;
    float fps = 0.f;
    bool loop = false;
};

int animationFrame(const Animation& animation, float elapsed);

// A character definition plus the pose of one character on the map.
class Character
{
public:
    std::string id;
    std::string name;
    CharacterAnchor anchor;
    float collisionRadius = 0.f;
    std::string defaultState;
    std::string defaultDirection;
    std::map<std::string, std::map<std::string, Animation>> animations;
    std::string state;
    std::string direction;
    float elapsed = 0.f;

    const Animation& animation(const std::string& state, const std::string& direction) const;
    void pose(const std::string& state, const std::string& direction);
    void walk(const std::string& direction);
    void idle(const std::string& direction);
    void advance(float seconds);
    int frame() const;
};
}
