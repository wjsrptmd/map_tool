#pragma once
#include "../model/Character.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace maptool
{
struct ActorSpawn
{
    std::string instanceId;
    bool player = false;
    bool collision = false;
    std::string interactionId;
    int chapter = 0;
    int mapX = 0;
    int mapY = 0;
    std::string state;
    std::string direction;
};

struct CharacterConfig
{
    int frameWidth = 0;
    int frameHeight = 0;
    int defaultFps = 0;
    bool textureSmooth = false;
};

// Character index, cached definitions, and who is on the current map.
class CharacterManager
{
public:
    struct Actor
    {
        int instance = 0;
        std::string instanceId;
        bool player = false;
        bool collision = false;
        std::string interactionId;
        int chapter = 0;
        int mapX = 0;
        int mapY = 0;
        int x = 0;
        int y = 0;
        Character body;
    };

    CharacterManager();
    CharacterManager(const CharacterManager&) = delete;
    CharacterManager& operator=(const CharacterManager&) = delete;
    CharacterManager(CharacterManager&&) = delete;
    CharacterManager& operator=(CharacterManager&&) = delete;

    const CharacterConfig& config() const noexcept { return config_; }
    bool contains(const std::string& id) const;
    const std::string& name(const std::string& id) const;
    const Character& character(const std::string& id);
    int appear(const std::string& id, int x, int y, const ActorSpawn& spawn);
    bool hasInstanceId(const std::string& id) const;
    void dismissOthers();
    void placeOnMap(int instance, int chapter, int mapX, int mapY);
    void move(int instance, int x, int y);
    Character& at(int instance);
    void advance(float seconds);
    const Actor& actor(int instance) const;
    const std::vector<Actor>& actors() const noexcept { return actors_; }

private:
    struct Entry
    {
        std::string name;
        std::filesystem::path definition;
    };

    void loadIndex();
    Character readCharacter(const std::string& id, const Entry& entry) const;
    Actor& findActor(int instance);

    std::filesystem::path charactersDir_;
    CharacterConfig config_;
    std::map<std::string, Entry> index_;
    std::map<std::string, Character> loaded_;
    std::vector<Actor> actors_;
    int nextInstance_ = 1;
};
}
