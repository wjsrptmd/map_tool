#include "CharacterService.hpp"
#include "../model/MapFile.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
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
    : maps_(maps), characters_(characters), random_(std::random_device{}())
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
            const MovementDefinition* definition = characters_.movement(actor.characterId);
            const bool resume = definition && motions_.contains(actor.instanceId);
            if (definition && !resume)
            {
                NpcMovementState state;
                state.origin = actor.position;
                state.position = actor.position;
                state.direction = actor.direction;
                motions_.emplace(actor.instanceId, state);
            }
            const Cell at = resume ? motions_.at(actor.instanceId).position : actor.position;
            ActorSpawn spawn;
            spawn.instanceId = actor.instanceId;
            spawn.collision = actor.collision;
            spawn.interactionId = actor.interactionId;
            spawn.chapter = chapter;
            spawn.mapX = mapX;
            spawn.mapY = mapY;
            spawn.state = actor.state;
            spawn.direction = actor.direction;
            const int instance = characters_.appear(actor.characterId, at.x, at.y, spawn);
            if (!resume) continue;
            const auto& state = motions_.at(actor.instanceId);
            auto& body = characters_.at(instance);
            if (state.walkRemaining > 0.f) body.walk(state.direction);
            else body.idle(state.direction);
        }
    }
}

void CharacterService::movePlayer(Cell step)
{
    if (step == Cell{} || maps_.loading()) return;
    const Cell before = playerCell();
    const Cell next = before + step;
    if (maps_.walkable(next) && !occupied(next, player_))
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

void CharacterService::advanceNpcs(float seconds)
{
    if (seconds <= 0.f || maps_.loading()) return;
    const int chapter = maps_.currentChapter();
    const int mapX = maps_.currentMapX();
    const int mapY = maps_.currentMapY();
    struct PendingStep
    {
        int instance = 0;
        std::string instanceId;
        Cell destination;
        std::string direction;
        float cycle = 0.f;
        std::size_t routeIndex = 0;
        bool followRoute = false;
    };
    std::vector<PendingStep> steps;
    std::vector<Cell> reserved;
    for (const auto& actor : characters_.actors())
    {
        if (actor.player || actor.chapter != chapter || actor.mapX != mapX || actor.mapY != mapY) continue;
        const auto found = motions_.find(actor.instanceId);
        if (found == motions_.end()) continue;
        auto& state = found->second;
        const MovementDefinition* move = characters_.movement(actor.body.id);
        if (!move) continue;
        const MovementDefinition definition = *move;
        if (state.walkRemaining > 0.f)
        {
            state.walkRemaining -= seconds;
            if (state.walkRemaining <= 0.f)
            {
                state.walkRemaining = 0.f;
                characters_.at(actor.instance).idle(state.direction);
            }
        }
        if (state.finished) continue;
        const float interval = 1.f / definition.speed;
        state.elapsed += seconds;
        if (state.elapsed < interval) continue;
        state.elapsed -= interval;
        if (state.elapsed >= interval) state.elapsed = 0.f;

        Cell destination{};
        std::size_t routeIndex = state.routeIndex;
        bool followRoute = false;
        if (definition.radius)
        {
            const Cell directions[] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
            destination = Cell{actor.x, actor.y} + directions[std::uniform_int_distribution<int>(0, 3)(random_)];
            const Cell offset = destination - state.origin;
            if (std::abs(offset.x) + std::abs(offset.y) > *definition.radius)
            {
                state.walkRemaining = 0.f;
                characters_.at(actor.instance).idle(state.direction);
                continue;
            }
        }
        else
        {
            std::size_t next = state.routeIndex + 1;
            if (next >= definition.route.size())
            {
                if (!definition.loop)
                {
                    state.finished = true;
                    state.walkRemaining = 0.f;
                    characters_.at(actor.instance).idle(state.direction);
                    continue;
                }
                next = 0;
            }
            const MovementStep relative = definition.route[next];
            destination = state.origin + Cell{relative.x, relative.y};
            routeIndex = next;
            followRoute = true;
        }
        const Cell step = destination - Cell{actor.x, actor.y};
        const bool held = std::find(reserved.begin(), reserved.end(), destination) != reserved.end();
        if (step == Cell{} || held || !standable(destination, actor.instance))
        {
            state.walkRemaining = 0.f;
            characters_.at(actor.instance).idle(state.direction);
            continue;
        }
        const auto facing = directionName(step);
        const Animation& animation = characters_.character(actor.body.id).animation("walk", facing);
        if (!(animation.fps > 0.f))
            throw std::runtime_error(actor.body.id + " walk/" + facing + " fps must be positive");
        reserved.push_back(destination);
        PendingStep pending;
        pending.instance = actor.instance;
        pending.instanceId = actor.instanceId;
        pending.destination = destination;
        pending.direction = facing;
        pending.cycle = static_cast<float>(animation.frames) / animation.fps;
        pending.routeIndex = routeIndex;
        pending.followRoute = followRoute;
        steps.push_back(std::move(pending));
    }
    for (const PendingStep& step : steps)
    {
        characters_.at(step.instance).walk(step.direction);
        characters_.move(step.instance, step.destination.x, step.destination.y);
        auto& state = motions_.at(step.instanceId);
        state.position = step.destination;
        state.direction = step.direction;
        state.walkRemaining = step.cycle;
        if (!step.followRoute) continue;
        state.routeIndex = step.routeIndex;
        const MovementDefinition& definition = *characters_.movement(characters_.actor(step.instance).body.id);
        if (!definition.loop && step.routeIndex + 1 == definition.route.size()) state.finished = true;
    }
}

bool CharacterService::npcCourse(const std::string& instanceId, NpcCourse& course) const
{
    const auto found = motions_.find(instanceId);
    if (found == motions_.end()) return false;
    course.origin = found->second.origin;
    course.position = found->second.position;
    course.routeIndex = found->second.routeIndex;
    course.finished = found->second.finished;
    return true;
}

bool CharacterService::standable(Cell cell, int ignoredInstance) const
{
    const auto& map = maps_.current();
    if (cell.x < 0 || cell.y < 0 || cell.x >= map.width || cell.y >= map.height) return false;
    if (!maps_.walkable(cell)) return false;
    return !occupied(cell, ignoredInstance);
}

bool CharacterService::occupied(Cell cell, int ignoredInstance) const
{
    for (const auto& actor : characters_.actors())
    {
        if (actor.instance == ignoredInstance || actor.x != cell.x || actor.y != cell.y) continue;
        if (actor.chapter != maps_.currentChapter() || actor.mapX != maps_.currentMapX() || actor.mapY != maps_.currentMapY())
            continue;
        if (actor.player || actor.collision) return true;
    }
    return false;
}
}
