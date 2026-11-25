#include "CharacterManager.hpp"
#include "../model/MapFile.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <system_error>
#include "../util/Log.hpp"

namespace maptool
{
namespace
{
void requireFields(const nlohmann::json& object, std::initializer_list<const char*> keys)
{
    if (!object.is_object()) maptool::fail("needs an object");
    for (const auto& item : object.items())
    {
        bool known = false;
        for (const char* key : keys)
            if (item.key() == key) known = true;
        if (!known) maptool::fail("unknown key " + item.key());
    }
    for (const char* key : keys)
        if (!object.contains(key)) maptool::fail(std::string(key) + " is required");
}

std::string requireText(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_string() || object[key].get<std::string>().empty())
        maptool::fail(std::string(key) + " must be text");
    return object[key].get<std::string>();
}

bool numericId(const std::string& id)
{
    for (const unsigned char c : id)
        if (c < '0' || c > '9') return false;
    return !id.empty();
}

int requireInt(const nlohmann::json& object, const char* key, int minValue)
{
    if (!object.contains(key) || !object[key].is_number_integer())
        maptool::fail(std::string(key) + " must be an integer");
    const int value = object[key].get<int>();
    if (value < minValue) maptool::fail(std::string(key) + " is out of range");
    return value;
}

float requireNumber(const nlohmann::json& object, const char* key, float minValue)
{
    if (!object.contains(key) || !object[key].is_number())
        maptool::fail(std::string(key) + " must be a number");
    const float value = object[key].get<float>();
    if (value < minValue) maptool::fail(std::string(key) + " is out of range");
    return value;
}

bool requireBool(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_boolean())
        maptool::fail(std::string(key) + " must be a boolean");
    return object[key].get<bool>();
}

int stepDistance(MovementStep left, MovementStep right)
{
    return std::abs(left.x - right.x) + std::abs(left.y - right.y);
}

MovementStep readStep(const nlohmann::json& value, const std::string& where)
{
    if (!value.is_object()) maptool::fail(where + " needs an object");
    for (const auto& item : value.items())
        if (item.key() != "x" && item.key() != "y")
            maptool::fail(where + " has unknown key " + item.key());
    if (!value.contains("x") || !value["x"].is_number_integer())
        maptool::fail(where + ".x must be an integer");
    if (!value.contains("y") || !value["y"].is_number_integer())
        maptool::fail(where + ".y must be an integer");
    return {value["x"].get<int>(), value["y"].get<int>()};
}

MovementDefinition readMove(const nlohmann::json& move)
{
    if (!move.is_object()) maptool::fail("move needs an object");
    const bool hasRoute = move.contains("route");
    const bool hasRadius = move.contains("radius");
    if (hasRoute && hasRadius) maptool::fail("move route and radius are both set");
    if (!hasRoute && !hasRadius) maptool::fail("move route or radius is required");
    for (const auto& item : move.items())
    {
        const auto& key = item.key();
        const bool routeKey = key == "speed" || key == "route" || key == "loop";
        const bool radiusKey = key == "speed" || key == "radius";
        if ((hasRoute && !routeKey) || (hasRadius && !radiusKey))
            maptool::fail("move has unknown key " + key);
    }
    if (!move.contains("speed") || !move["speed"].is_number())
        maptool::fail("move.speed must be a positive number");
    const float speed = move["speed"].get<float>();
    if (!(speed > 0.f)) maptool::fail("move.speed must be a positive number");

    MovementDefinition result;
    result.speed = speed;
    if (hasRadius)
    {
        if (!move["radius"].is_number_integer()) maptool::fail("move.radius must be an integer");
        const int radius = move["radius"].get<int>();
        if (radius < 0) maptool::fail("move.radius is out of range");
        result.radius = radius;
        return result;
    }
    if (!move.contains("loop") || !move["loop"].is_boolean())
        maptool::fail("move.loop must be a boolean");
    result.loop = move["loop"].get<bool>();
    if (!move["route"].is_array()) maptool::fail("move.route must be an array");
    if (move["route"].size() < 2) maptool::fail("move.route needs at least two points");
    for (std::size_t index = 0; index < move["route"].size(); ++index)
    {
        const auto where = "move.route[" + std::to_string(index) + "]";
        const MovementStep step = readStep(move["route"][index], where);
        if (index == 0 && (step.x != 0 || step.y != 0))
            maptool::fail(where + " must be the origin");
        if (index > 0 && stepDistance(result.route[index - 1], step) != 1)
            maptool::fail(where + " is not adjacent");
        result.route.push_back(step);
    }
    if (result.loop && stepDistance(result.route.back(), result.route.front()) != 1)
        maptool::fail("move.route is not a loop");
    return result;
}

std::filesystem::path fileInside(const std::filesystem::path& root, const std::filesystem::path& base, const std::string& text)
{
    const std::filesystem::path relative(text);
    if (relative.empty() || relative.is_absolute())
        maptool::fail("path must stay inside characters");
    const auto combined = base / relative;
    std::error_code error;
    if (!std::filesystem::is_regular_file(combined, error))
        maptool::fail("file not found: " + combined.generic_string());
    error.clear();
    const auto full = std::filesystem::weakly_canonical(combined, error);
    if (error) maptool::fail("file not found: " + combined.generic_string());
    error.clear();
    const auto rootPath = std::filesystem::weakly_canonical(root, error);
    if (error) maptool::fail("characters not found");
    error.clear();
    const auto inner = std::filesystem::relative(full, rootPath, error);
    if (error || inner.empty() || inner.begin()->generic_string() == "..")
        maptool::fail("path must stay inside characters");
    return full;
}
}

CharacterManager::CharacterManager()
{
    const auto project = projectDirectory();
    charactersDir_ = project / "characters";
    std::error_code error;
    if (!std::filesystem::is_directory(charactersDir_, error))
        maptool::fail("characters not found: " + charactersDir_.generic_string());
    loadIndex();
}

bool CharacterManager::contains(const std::string& id) const
{
    return index_.contains(id);
}

const std::string& CharacterManager::name(const std::string& id) const
{
    const auto found = index_.find(id);
    if (found == index_.end()) maptool::failRange("unknown character " + id);
    return found->second.name;
}

const Character& CharacterManager::character(const std::string& id)
{
    if (const auto found = loaded_.find(id); found != loaded_.end()) return found->second;
    const auto entry = index_.find(id);
    if (entry == index_.end()) maptool::failRange("unknown character " + id);
    const auto& loaded = loaded_.emplace(id, readCharacter(id, entry->second)).first->second;
    Log::info("loaded character " + loaded.id + " " + loaded.name + (loaded.movement ? " with move" : " without move"));
    return loaded;
}

const MovementDefinition* CharacterManager::movement(const std::string& id)
{
    const auto& definition = character(id).movement;
    if (!definition) return nullptr;
    return &(*definition);
}

void CharacterManager::checkMovement(const std::string& text, const std::string& label) const
{
    try
    {
        readMove(nlohmann::json::parse(text));
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const auto prefix = label + ": ";
        if (message.starts_with(prefix)) throw;
        maptool::fail(prefix + message);
    }
}

int CharacterManager::appear(const std::string& id, int x, int y, const ActorSpawn& spawn)
{
    if (!spawn.instanceId.empty() && hasInstanceId(spawn.instanceId))
    {
        for (const auto& actor : actors_)
            if (actor.instanceId == spawn.instanceId) return actor.instance;
    }
    Actor actor;
    actor.instance = nextInstance_++;
    actor.instanceId = spawn.instanceId;
    actor.player = spawn.player;
    actor.collision = spawn.collision;
    actor.interactionId = spawn.interactionId;
    actor.chapter = spawn.chapter;
    actor.mapX = spawn.mapX;
    actor.mapY = spawn.mapY;
    actor.x = x;
    actor.y = y;
    actor.body = character(id);
    actor.body.state = actor.body.defaultState;
    actor.body.direction = actor.body.defaultDirection;
    actor.body.elapsed = 0.f;
    if (!spawn.state.empty()) actor.body.pose(spawn.state, spawn.direction);
    const int instance = actor.instance;
    Log::info(std::string(spawn.player ? "appear player " : "appear npc ") + id
        + " instance " + std::to_string(instance)
        + (spawn.instanceId.empty() ? "" : " " + spawn.instanceId)
        + " at " + std::to_string(x) + "," + std::to_string(y)
        + " " + actor.body.state + "/" + actor.body.direction
        + (spawn.collision ? " collision" : ""));
    actors_.push_back(std::move(actor));
    return instance;
}

bool CharacterManager::hasInstanceId(const std::string& id) const
{
    if (id.empty()) return false;
    for (const auto& actor : actors_)
        if (actor.instanceId == id) return true;
    return false;
}

void CharacterManager::dismissOthers()
{
    actors_.erase(std::remove_if(actors_.begin(), actors_.end(), [](const Actor& actor) { return !actor.player; }), actors_.end());
}

void CharacterManager::placeOnMap(int instance, int chapter, int mapX, int mapY)
{
    auto& actor = findActor(instance);
    actor.chapter = chapter;
    actor.mapX = mapX;
    actor.mapY = mapY;
}

void CharacterManager::move(int instance, int x, int y)
{
    auto& actor = findActor(instance);
    actor.x = x;
    actor.y = y;
}

Character& CharacterManager::at(int instance)
{
    return findActor(instance).body;
}

void CharacterManager::advance(float seconds)
{
    if (seconds <= 0.f) return;
    for (auto& actor : actors_) actor.body.advance(seconds);
}

const CharacterManager::Actor& CharacterManager::actor(int instance) const
{
    for (const auto& actor : actors_)
        if (actor.instance == instance) return actor;
    maptool::failRange("unknown character instance");
}

CharacterManager::Actor& CharacterManager::findActor(int instance)
{
    for (auto& actor : actors_)
        if (actor.instance == instance) return actor;
    maptool::failRange("unknown character instance");
}

void CharacterManager::loadIndex()
{
    const auto file = charactersDir_ / "character_info.json";
    try
    {
        std::ifstream in(file);
        if (!in) maptool::fail("failed to open " + file.generic_string());
        const auto root = nlohmann::json::parse(in);
        requireFields(root, {"globalConfig", "characters"});
        const auto& config = root["globalConfig"];
        requireFields(config, {"frameWidth", "frameHeight", "defaultFps", "textureSmooth"});
        config_.frameWidth = requireInt(config, "frameWidth", 1);
        config_.frameHeight = requireInt(config, "frameHeight", 1);
        config_.defaultFps = requireInt(config, "defaultFps", 0);
        config_.textureSmooth = requireBool(config, "textureSmooth");
        if (!root["characters"].is_array() || root["characters"].empty())
            maptool::fail("needs characters");
        for (const auto& value : root["characters"])
        {
            requireFields(value, {"id", "name", "definition"});
            const auto id = requireText(value, "id");
            if (!numericId(id)) maptool::fail(id + " is not a numeric character id");
            if (index_.contains(id)) maptool::fail("repeats character " + id);
            Entry entry;
            entry.name = requireText(value, "name");
            entry.definition = fileInside(charactersDir_, charactersDir_, requireText(value, "definition"));
            index_.emplace(id, std::move(entry));
        }
        Log::info("character index " + std::to_string(index_.size())
            + " frame " + std::to_string(config_.frameWidth) + "x" + std::to_string(config_.frameHeight));
    }
    catch (const std::exception& error)
    {
        maptool::fail(file.generic_string() + ": " + error.what());
    }
}

Character CharacterManager::readCharacter(const std::string& id, const Entry& entry) const
{
    const auto& file = entry.definition;
    try
    {
        std::ifstream in(file);
        if (!in) maptool::fail("failed to open " + file.generic_string());
        const auto root = nlohmann::json::parse(in);
        if (!root.is_object()) maptool::fail("needs an object");
        for (const auto& item : root.items())
        {
            const auto& key = item.key();
            if (key != "id" && key != "name" && key != "defaultState" && key != "defaultDirection"
                && key != "anchor" && key != "collisionRadius" && key != "animations" && key != "move")
                maptool::fail("unknown key " + key);
        }
        for (const char* key : {"id", "name", "defaultState", "defaultDirection", "anchor", "collisionRadius", "animations"})
            if (!root.contains(key)) maptool::fail(std::string(key) + " is required");
        Character character;
        character.id = requireText(root, "id");
        character.name = requireText(root, "name");
        if (character.id != id) maptool::fail("character id does not match the index");
        if (character.name != entry.name) maptool::fail("character name does not match the index");
        const auto& anchor = root["anchor"];
        if (!anchor.is_array() || anchor.size() != 2 || !anchor[0].is_number() || !anchor[1].is_number())
            maptool::fail("anchor must be [x, y]");
        character.anchor = {anchor[0].get<float>(), anchor[1].get<float>()};
        character.collisionRadius = requireNumber(root, "collisionRadius", 0.f);
        if (character.collisionRadius <= 0.f) maptool::fail("collisionRadius must be positive");
        character.defaultState = requireText(root, "defaultState");
        character.defaultDirection = requireText(root, "defaultDirection");
        if (!root["animations"].is_object() || root["animations"].empty())
            maptool::fail("needs animations");
        const auto folder = file.parent_path();
        for (const auto& state : root["animations"].items())
        {
            if (state.key().empty() || !state.value().is_object() || state.value().empty())
                maptool::fail("animation state needs directions");
            for (const auto& direction : state.value().items())
            {
                if (direction.key().empty()) maptool::fail("animation direction is empty");
                requireFields(direction.value(), {"image", "frames", "fps", "loop"});
                Animation animation;
                animation.image = fileInside(charactersDir_, folder, requireText(direction.value(), "image"));
                animation.frames = requireInt(direction.value(), "frames", 1);
                animation.fps = requireNumber(direction.value(), "fps", 0.f);
                animation.loop = requireBool(direction.value(), "loop");
                character.animations[state.key()].emplace(direction.key(), std::move(animation));
            }
        }
        character.animation(character.defaultState, character.defaultDirection);
        if (root.contains("move")) character.movement = readMove(root["move"]);
        return character;
    }
    catch (const std::exception& error)
    {
        maptool::fail(file.generic_string() + ": " + error.what());
    }
}
}
