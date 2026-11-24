#include "CharacterManager.hpp"
#include "../model/MapFile.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <system_error>

namespace maptool
{
namespace
{
void requireFields(const nlohmann::json& object, std::initializer_list<const char*> keys)
{
    if (!object.is_object()) throw std::runtime_error("needs an object");
    for (const auto& item : object.items())
    {
        bool known = false;
        for (const char* key : keys)
            if (item.key() == key) known = true;
        if (!known) throw std::runtime_error("unknown key " + item.key());
    }
    for (const char* key : keys)
        if (!object.contains(key)) throw std::runtime_error(std::string(key) + " is required");
}

std::string requireText(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_string() || object[key].get<std::string>().empty())
        throw std::runtime_error(std::string(key) + " must be text");
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
        throw std::runtime_error(std::string(key) + " must be an integer");
    const int value = object[key].get<int>();
    if (value < minValue) throw std::runtime_error(std::string(key) + " is out of range");
    return value;
}

float requireNumber(const nlohmann::json& object, const char* key, float minValue)
{
    if (!object.contains(key) || !object[key].is_number())
        throw std::runtime_error(std::string(key) + " must be a number");
    const float value = object[key].get<float>();
    if (value < minValue) throw std::runtime_error(std::string(key) + " is out of range");
    return value;
}

bool requireBool(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_boolean())
        throw std::runtime_error(std::string(key) + " must be a boolean");
    return object[key].get<bool>();
}

std::filesystem::path fileInside(const std::filesystem::path& root, const std::filesystem::path& base, const std::string& text)
{
    const std::filesystem::path relative(text);
    if (relative.empty() || relative.is_absolute())
        throw std::runtime_error("path must stay inside characters");
    const auto combined = base / relative;
    std::error_code error;
    if (!std::filesystem::is_regular_file(combined, error))
        throw std::runtime_error("file not found: " + combined.generic_string());
    error.clear();
    const auto full = std::filesystem::weakly_canonical(combined, error);
    if (error) throw std::runtime_error("file not found: " + combined.generic_string());
    error.clear();
    const auto rootPath = std::filesystem::weakly_canonical(root, error);
    if (error) throw std::runtime_error("characters not found");
    error.clear();
    const auto inner = std::filesystem::relative(full, rootPath, error);
    if (error || inner.empty() || inner.begin()->generic_string() == "..")
        throw std::runtime_error("path must stay inside characters");
    return full;
}
}

CharacterManager::CharacterManager()
{
    const auto project = projectDirectory();
    charactersDir_ = project / "characters";
    std::error_code error;
    if (!std::filesystem::is_directory(charactersDir_, error))
        throw std::runtime_error("characters not found: " + charactersDir_.generic_string());
    loadIndex();
}

bool CharacterManager::contains(const std::string& id) const
{
    return index_.contains(id);
}

const std::string& CharacterManager::name(const std::string& id) const
{
    const auto found = index_.find(id);
    if (found == index_.end()) throw std::out_of_range("unknown character " + id);
    return found->second.name;
}

const Character& CharacterManager::character(const std::string& id)
{
    if (const auto found = loaded_.find(id); found != loaded_.end()) return found->second;
    const auto entry = index_.find(id);
    if (entry == index_.end()) throw std::out_of_range("unknown character " + id);
    return loaded_.emplace(id, readCharacter(id, entry->second)).first->second;
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
    throw std::out_of_range("unknown character instance");
}

CharacterManager::Actor& CharacterManager::findActor(int instance)
{
    for (auto& actor : actors_)
        if (actor.instance == instance) return actor;
    throw std::out_of_range("unknown character instance");
}

void CharacterManager::loadIndex()
{
    const auto file = charactersDir_ / "character_info.json";
    try
    {
        std::ifstream in(file);
        if (!in) throw std::runtime_error("failed to open " + file.generic_string());
        const auto root = nlohmann::json::parse(in);
        requireFields(root, {"globalConfig", "characters"});
        const auto& config = root["globalConfig"];
        requireFields(config, {"frameWidth", "frameHeight", "defaultFps", "textureSmooth"});
        config_.frameWidth = requireInt(config, "frameWidth", 1);
        config_.frameHeight = requireInt(config, "frameHeight", 1);
        config_.defaultFps = requireInt(config, "defaultFps", 0);
        config_.textureSmooth = requireBool(config, "textureSmooth");
        if (!root["characters"].is_array() || root["characters"].empty())
            throw std::runtime_error("needs characters");
        for (const auto& value : root["characters"])
        {
            requireFields(value, {"id", "name", "definition"});
            const auto id = requireText(value, "id");
            if (!numericId(id)) throw std::runtime_error(id + " is not a numeric character id");
            if (index_.contains(id)) throw std::runtime_error("repeats character " + id);
            Entry entry;
            entry.name = requireText(value, "name");
            entry.definition = fileInside(charactersDir_, charactersDir_, requireText(value, "definition"));
            index_.emplace(id, std::move(entry));
        }
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(file.generic_string() + ": " + error.what());
    }
}

Character CharacterManager::readCharacter(const std::string& id, const Entry& entry) const
{
    const auto& file = entry.definition;
    try
    {
        std::ifstream in(file);
        if (!in) throw std::runtime_error("failed to open " + file.generic_string());
        const auto root = nlohmann::json::parse(in);
        requireFields(root, {"id", "name", "defaultState", "defaultDirection", "anchor", "collisionRadius", "animations"});
        Character character;
        character.id = requireText(root, "id");
        character.name = requireText(root, "name");
        if (character.id != id) throw std::runtime_error("character id does not match the index");
        if (character.name != entry.name) throw std::runtime_error("character name does not match the index");
        const auto& anchor = root["anchor"];
        if (!anchor.is_array() || anchor.size() != 2 || !anchor[0].is_number() || !anchor[1].is_number())
            throw std::runtime_error("anchor must be [x, y]");
        character.anchor = {anchor[0].get<float>(), anchor[1].get<float>()};
        character.collisionRadius = requireNumber(root, "collisionRadius", 0.f);
        if (character.collisionRadius <= 0.f) throw std::runtime_error("collisionRadius must be positive");
        character.defaultState = requireText(root, "defaultState");
        character.defaultDirection = requireText(root, "defaultDirection");
        if (!root["animations"].is_object() || root["animations"].empty())
            throw std::runtime_error("needs animations");
        const auto folder = file.parent_path();
        for (const auto& state : root["animations"].items())
        {
            if (state.key().empty() || !state.value().is_object() || state.value().empty())
                throw std::runtime_error("animation state needs directions");
            for (const auto& direction : state.value().items())
            {
                if (direction.key().empty()) throw std::runtime_error("animation direction is empty");
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
        return character;
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(file.generic_string() + ": " + error.what());
    }
}
}
