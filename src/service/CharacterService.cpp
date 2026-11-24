#include "CharacterService.hpp"
#include "../model/MapFile.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace maptool
{
namespace
{
std::string directionName(Cell step)
{
    if (step.y < 0) return "up";
    if (step.y > 0) return "down";
    if (step.x < 0) return "left";
    return "right";
}

bool isDirection(const std::string& value)
{
    return value == "up" || value == "down" || value == "left" || value == "right";
}

int chapterIndex(const std::string& key)
{
    constexpr std::string_view prefix = "chapter";
    if (!key.starts_with(prefix)) return 0;
    const auto number = key.substr(prefix.size());
    if (number.empty() || number.front() == '0') return 0;
    int value = 0;
    for (const char c : number)
    {
        if (c < '0' || c > '9') return 0;
        if (value > 1000000) return 0;
        value = value * 10 + (c - '0');
    }
    return value;
}

void requireFields(const nlohmann::json& object, std::initializer_list<const char*> keys, const std::string& where)
{
    if (!object.is_object()) throw std::runtime_error(where + " needs an object");
    for (const auto& item : object.items())
    {
        bool known = false;
        for (const char* key : keys)
            if (item.key() == key) known = true;
        if (!known) throw std::runtime_error(where + " has unknown key " + item.key());
    }
    for (const char* key : keys)
        if (!object.contains(key)) throw std::runtime_error(where + " " + std::string(key) + " is required");
}

std::string requireText(const nlohmann::json& object, const char* key, const std::string& where)
{
    if (!object.contains(key) || !object[key].is_string() || object[key].get<std::string>().empty())
        throw std::runtime_error(where + "." + key + " must be text");
    return object[key].get<std::string>();
}

int requireInt(const nlohmann::json& object, const char* key, const std::string& where)
{
    if (!object.contains(key) || !object[key].is_number_integer())
        throw std::runtime_error(where + "." + key + " must be an integer");
    return object[key].get<int>();
}

bool requireBool(const nlohmann::json& object, const char* key, const std::string& where)
{
    if (!object.contains(key) || !object[key].is_boolean())
        throw std::runtime_error(where + "." + key + " must be a boolean");
    return object[key].get<bool>();
}

const nlohmann::json& requireObject(const nlohmann::json& object, const char* key, const std::string& where)
{
    if (!object.contains(key) || !object[key].is_object())
        throw std::runtime_error(where + "." + key + " needs an object");
    return object[key];
}

Cell requireCell(const nlohmann::json& object, const std::string& where)
{
    requireFields(object, {"x", "y"}, where);
    return {requireInt(object, "x", where), requireInt(object, "y", where)};
}

std::string placementKey(int mapX, int mapY, int x, int y)
{
    return std::to_string(mapX) + "," + std::to_string(mapY) + "," + std::to_string(x) + "," + std::to_string(y);
}
}

CharacterService::CharacterService(MapManger& maps, CharacterManager& characters)
    : maps_(maps), characters_(characters)
{
}

void CharacterService::placePlayer(const std::string& characterId, int chapter, Cell startMap)
{
    const auto file = projectDirectory() / "maps" / "world" / "character_placements.json";
    std::ifstream in(file);
    if (!in) throw std::runtime_error("failed to open " + file.generic_string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    chapters_ = readPlacements(buffer.str(), file, chapter, startMap, characterId);
    maps_.setCurrentMap(startMap.x, startMap.y);
    const auto& placed = chapters_.at(chapter);
    ActorSpawn spawn;
    spawn.player = true;
    spawn.chapter = chapter;
    spawn.mapX = startMap.x;
    spawn.mapY = startMap.y;
    spawn.state = "idle";
    spawn.direction = placed.direction;
    player_ = characters_.appear(characterId, placed.player.x, placed.player.y, spawn);
    enterMap();
}

void CharacterService::enterMap()
{
    const int chapter = maps_.currentChapter();
    const int mapX = maps_.currentMapX();
    const int mapY = maps_.currentMapY();
    if (player_ >= 0) characters_.placeOnMap(player_, chapter, mapX, mapY);
    characters_.dismissOthers();
    const auto found = chapters_.find(chapter);
    if (found == chapters_.end()) return;
    for (const auto& map : found->second.maps)
    {
        if (map.x != mapX || map.y != mapY) continue;
        for (const auto& actor : map.actors)
        {
            if (!actor.enabled || characters_.hasInstanceId(actor.instanceId)) continue;
            ActorSpawn spawn;
            spawn.instanceId = actor.instanceId;
            spawn.collision = actor.collision;
            spawn.interactionId = actor.interactionId;
            spawn.chapter = chapter;
            spawn.mapX = mapX;
            spawn.mapY = mapY;
            spawn.state = actor.state;
            spawn.direction = actor.direction;
            characters_.appear(actor.characterId, actor.position.x, actor.position.y, spawn);
        }
    }
}

void CharacterService::movePlayer(Cell step)
{
    if (step == Cell{} || maps_.loading()) return;
    const Cell before = playerCell();
    const Cell next = before + step;
    if (maps_.walkable(next) && !occupied(next))
        characters_.move(player_, next.x, next.y);
    else if (!maps_.walkable(next))
        maps_.beginNeighbor(next);
    const bool moved = playerCell() != before;
    auto& body = characters_.at(player_);
    const auto facing = directionName(step);
    if (moved) body.walk(facing);
    else body.idle(facing);
}

void CharacterService::stopPlayer()
{
    auto& body = characters_.at(player_);
    body.idle(body.direction);
}

void CharacterService::arrive(Cell cell)
{
    characters_.move(player_, cell.x, cell.y);
    enterMap();
}

Cell CharacterService::playerCell() const
{
    const auto& actor = characters_.actor(player_);
    return {actor.x, actor.y};
}

void CharacterService::validatePlacement(const std::string& text, const std::string& label, int chapter, Cell startMap, const std::string& characterId)
{
    readPlacements(text, label, chapter, startMap, characterId);
}

std::map<int, CharacterService::PlacedChapter> CharacterService::readPlacements(const std::string& text, const std::filesystem::path& file, int chapter, Cell startMap, const std::string& characterId)
{
    try
    {
        auto chapters = parseText(text);
        validate(chapters, chapter, startMap, characterId);
        return chapters;
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const auto prefix = file.generic_string() + ": ";
        if (message.starts_with(prefix)) throw;
        throw std::runtime_error(prefix + message);
    }
}

std::map<int, CharacterService::PlacedChapter> CharacterService::parseText(const std::string& text)
{
    const auto root = nlohmann::json::parse(text);
    if (!root.is_object()) throw std::runtime_error("needs an object");
    std::map<int, PlacedChapter> chapters;
    std::map<std::string, std::string> instances;
    for (const auto& item : root.items())
    {
        const int number = chapterIndex(item.key());
        if (number <= 0) throw std::runtime_error(item.key() + " is not a chapter");
        requireFields(item.value(), {"playerStart", "maps"}, item.key());
        PlacedChapter chapter;
        const auto& start = requireObject(item.value(), "playerStart", item.key());
        requireFields(start, {"position", "direction"}, item.key() + ".playerStart");
        chapter.player = requireCell(requireObject(start, "position", item.key() + ".playerStart"), item.key() + ".playerStart.position");
        chapter.direction = requireText(start, "direction", item.key() + ".playerStart");
        if (!item.value()["maps"].is_array()) throw std::runtime_error(item.key() + ".maps must be an array");
        std::size_t mapIndex = 0;
        for (const auto& mapValue : item.value()["maps"])
        {
            const auto mapWhere = item.key() + ".maps[" + std::to_string(mapIndex++) + "]";
            requireFields(mapValue, {"map", "actors"}, mapWhere);
            const auto& coordinates = requireObject(mapValue, "map", mapWhere);
            requireFields(coordinates, {"x", "y"}, mapWhere + ".map");
            PlacedMap map;
            map.x = requireInt(coordinates, "x", mapWhere + ".map");
            map.y = requireInt(coordinates, "y", mapWhere + ".map");
            if (map.x < 0 || map.y < 0) throw std::runtime_error(mapWhere + ".map is outside the map");
            if (!mapValue["actors"].is_array()) throw std::runtime_error(mapWhere + ".actors must be an array");
            std::size_t actorIndex = 0;
            for (const auto& actorValue : mapValue["actors"])
            {
                const auto actorWhere = mapWhere + ".actors[" + std::to_string(actorIndex++) + "]";
                requireFields(actorValue, {"instanceId", "characterId", "position", "direction", "state", "collision", "enabled", "interactionId"}, actorWhere);
                PlacedActor actor;
                actor.instanceId = requireText(actorValue, "instanceId", actorWhere);
                if (instances.contains(actor.instanceId))
                    throw std::runtime_error(actorWhere + ".instanceId repeats " + actor.instanceId);
                instances.emplace(actor.instanceId, actorWhere);
                actor.characterId = requireText(actorValue, "characterId", actorWhere);
                actor.position = requireCell(requireObject(actorValue, "position", actorWhere), actorWhere + ".position");
                actor.direction = requireText(actorValue, "direction", actorWhere);
                actor.state = requireText(actorValue, "state", actorWhere);
                actor.collision = requireBool(actorValue, "collision", actorWhere);
                actor.enabled = requireBool(actorValue, "enabled", actorWhere);
                actor.interactionId = requireText(actorValue, "interactionId", actorWhere);
                map.actors.push_back(std::move(actor));
            }
            chapter.maps.push_back(std::move(map));
        }
        chapters.emplace(number, std::move(chapter));
    }
    return chapters;
}

void CharacterService::validate(const std::map<int, PlacedChapter>& chapters, int chapter, Cell startMap, const std::string& characterId)
{
    const auto selected = chapters.find(chapter);
    if (selected == chapters.end()) throw std::runtime_error("chapter" + std::to_string(chapter) + " is required");
    for (const auto& [number, placed] : chapters)
    {
        const auto chapterWhere = "chapter" + std::to_string(number);
        if (!isDirection(placed.direction))
            throw std::runtime_error(chapterWhere + ".playerStart.direction is not a direction");
        try
        {
            characters_.character(characterId).animation("idle", placed.direction);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(chapterWhere + ".playerStart.direction " + error.what());
        }
        for (std::size_t i = 0; i < placed.maps.size(); ++i)
        {
            const auto& map = placed.maps[i];
            for (std::size_t j = 0; j < map.actors.size(); ++j)
            {
                const auto& actor = map.actors[j];
                const auto where = chapterWhere + ".maps[" + std::to_string(i) + "].actors[" + std::to_string(j) + "]";
                if (!characters_.contains(actor.characterId))
                    throw std::runtime_error(where + ".characterId is unknown");
                if (!isDirection(actor.direction))
                    throw std::runtime_error(where + ".direction is not a direction");
                try
                {
                    characters_.character(actor.characterId).animation(actor.state, actor.direction);
                }
                catch (const std::exception& error)
                {
                    throw std::runtime_error(where + " " + error.what());
                }
            }
        }
    }

    const int backX = maps_.currentMapX();
    const int backY = maps_.currentMapY();
    try
    {
        std::map<std::string, std::string> occupied;
        const auto& placed = selected->second;
        const auto chapterWhere = "chapter" + std::to_string(chapter);
        for (std::size_t i = 0; i < placed.maps.size(); ++i)
        {
            const auto& map = placed.maps[i];
            const auto mapWhere = chapterWhere + ".maps[" + std::to_string(i) + "]";
            try
            {
                maps_.setCurrentMap(map.x, map.y);
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error(mapWhere + ".map " + error.what());
            }
            for (std::size_t j = 0; j < map.actors.size(); ++j)
            {
                const auto& actor = map.actors[j];
                const auto where = mapWhere + ".actors[" + std::to_string(j) + "].position";
                const auto width = maps_.current().width;
                const auto height = maps_.current().height;
                if (actor.position.x < 0 || actor.position.y < 0 || actor.position.x >= width || actor.position.y >= height)
                    throw std::runtime_error(where + " is outside the map");
                if (!maps_.walkable(actor.position))
                    throw std::runtime_error(where + " is blocked");
                if (!actor.enabled || !actor.collision) continue;
                const auto key = placementKey(map.x, map.y, actor.position.x, actor.position.y);
                if (occupied.contains(key))
                    throw std::runtime_error(where + " overlaps another character");
                occupied.emplace(key, where);
            }
        }
        try
        {
            maps_.setCurrentMap(startMap.x, startMap.y);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(chapterWhere + ".playerStart " + error.what());
        }
        const auto width = maps_.current().width;
        const auto height = maps_.current().height;
        if (placed.player.x < 0 || placed.player.y < 0 || placed.player.x >= width || placed.player.y >= height)
            throw std::runtime_error(chapterWhere + ".playerStart.position is outside the map");
        if (!maps_.walkable(placed.player))
            throw std::runtime_error(chapterWhere + ".playerStart.position is blocked");
        if (occupied.contains(placementKey(startMap.x, startMap.y, placed.player.x, placed.player.y)))
            throw std::runtime_error(chapterWhere + ".playerStart.position overlaps another character");
        maps_.setCurrentMap(backX, backY);
    }
    catch (...)
    {
        try { maps_.setCurrentMap(backX, backY); }
        catch (...) {}
        throw;
    }
}

bool CharacterService::occupied(Cell cell) const
{
    for (const auto& actor : characters_.actors())
    {
        if (actor.player || !actor.collision) continue;
        if (actor.chapter != maps_.currentChapter() || actor.mapX != maps_.currentMapX() || actor.mapY != maps_.currentMapY())
            continue;
        if (actor.x == cell.x && actor.y == cell.y) return true;
    }
    return false;
}
}
