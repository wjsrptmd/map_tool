#pragma once
#include "../manager/CharacterManager.hpp"
#include "../manager/MapManger.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace maptool
{
// Places characters. The map module does not know who stands on it.
class CharacterService
{
public:
    CharacterService(MapManger& maps, CharacterManager& characters);

    void placePlayer(const std::string& characterId, int chapter, Cell startMap);
    void enterMap();
    void movePlayer(Cell step);
    void stopPlayer();
    void arrive(Cell cell);
    Cell playerCell() const;
    void validatePlacement(const std::string& text, const std::string& label, int chapter, Cell startMap, const std::string& characterId);

    int player() const noexcept { return player_; }

private:
    struct PlacedActor
    {
        std::string instanceId;
        std::string characterId;
        Cell position;
        std::string direction;
        std::string state;
        bool collision = false;
        bool enabled = false;
        std::string interactionId;
    };
    struct PlacedMap
    {
        int x = 0;
        int y = 0;
        std::vector<PlacedActor> actors;
    };
    struct PlacedChapter
    {
        Cell player;
        std::string direction;
        std::vector<PlacedMap> maps;
    };

    std::map<int, PlacedChapter> readPlacements(const std::string& text, const std::filesystem::path& file, int chapter, Cell startMap, const std::string& characterId);
    static std::map<int, PlacedChapter> parseText(const std::string& text);
    void validate(const std::map<int, PlacedChapter>& chapters, int chapter, Cell startMap, const std::string& characterId);
    bool occupied(Cell cell) const;

    MapManger& maps_;
    CharacterManager& characters_;
    std::map<int, PlacedChapter> chapters_;
    int player_ = -1;
};
}
