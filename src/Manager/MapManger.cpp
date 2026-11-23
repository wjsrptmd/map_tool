#include "MapManger.hpp"
#include "MapFile.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace maptool
{
namespace
{
void requireKnownTiles(const std::map<std::string, TileInfo>& tileInfo, const MapFile& map, const std::filesystem::path& file)
{
    for (const auto& row : map.tiles)
        for (const auto& id : row)
            if (!tileInfo.contains(id))
                throw std::runtime_error(file.filename().string() + " has unknown tile " + id);
}

bool isTileId(const std::string& id)
{
    if (id.empty()) return false;
    for (const unsigned char c : id)
        if (c < '0' || c > '9') return false;
    return true;
}

std::string requireText(const nlohmann::json& object, const std::string& id, const char* key)
{
    if (!object.contains(key) || !object[key].is_string())
        throw std::runtime_error(id + " needs " + key);
    auto text = object[key].get<std::string>();
    if (text.empty()) throw std::runtime_error(id + " has an empty " + key);
    return text;
}

float requireNumber(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_number())
        throw std::runtime_error(std::string(key) + " must be a number");
    const float value = object[key].get<float>();
    if (value <= 0.f) throw std::runtime_error(std::string(key) + " must be positive");
    return value;
}

Color requireColor(const nlohmann::json& object, const char* key)
{
    if (!object.contains(key) || !object[key].is_array() || object[key].size() != 3)
        throw std::runtime_error(std::string(key) + " must be [r, g, b]");
    std::uint8_t channels[3] = {};
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& channel = object[key][i];
        if (!channel.is_number_integer()) throw std::runtime_error(std::string(key) + " channels must be integers");
        const int value = channel.get<int>();
        if (value < 0 || value > 255) throw std::runtime_error(std::string(key) + " channels must be 0 to 255");
        channels[i] = static_cast<std::uint8_t>(value);
    }
    return {channels[0], channels[1], channels[2]};
}
}

MapManger::MapManger() : mapDir_(projectDirectory())
{
    projectName_ = mapDir_.filename().string();
    loadTileInfo();
    const auto worldFiles = worldMapFiles(mapDir_ / "world");
    for (const auto& file : worldFiles)
    {
        auto map = loadMapFile(file.path);
        if (map.kind != "world") throw std::runtime_error(file.path.filename().string() + " is not a world map");
        requireKnownTiles(currentMap_, map, file.path);
    }
    setCurrentMap(worldFiles.front().x, worldFiles.front().y);
}
bool MapManger::worldFileExists(int x, int y) const noexcept
{
    std::error_code error;
    return std::filesystem::is_regular_file(worldFilePath(x, y), error);
}
std::filesystem::path MapManger::worldFilePath(int x, int y) const
{
    return mapDir_ / "world" / (std::to_string(y) + "_" + std::to_string(x) + ".map");
}
void MapManger::setCurrentMap(int x, int y)
{
    if (!worldFileExists(x, y)) throw std::out_of_range("World map does not exist");
    const auto path = worldFilePath(x, y);
    auto loaded = loadMapFile(path);
    if (loaded.kind != "world") throw std::runtime_error(path.filename().string() + " is not a world map");
    requireKnownTiles(currentMap_, loaded, path);
    MapData map;
    map.name = std::move(loaded.name);
    map.width = loaded.width;
    map.height = loaded.height;
    map.tiles = std::move(loaded.tiles);
    adopt(x, y, std::move(map));
}
void MapManger::adopt(int x, int y, MapData map)
{
    current_ = std::move(map);
    currentX_ = x;
    currentY_ = y;
    focus_ = {};
    refreshView();
}
std::optional<Cell> MapManger::enterNeighbor(Cell cell)
{
    const bool insideX = cell.x >= 0 && cell.x < current_.width;
    const bool insideY = cell.y >= 0 && cell.y < current_.height;
    int nextX = currentX_;
    int nextY = currentY_;
    int landX = cell.x;
    int landY = cell.y;
    if (insideX && cell.y < 0)
        --nextY;
    else if (insideX && cell.y >= current_.height)
        ++nextY;
    else if (insideY && cell.x < 0)
        --nextX;
    else if (insideY && cell.x >= current_.width)
        ++nextX;
    else
        return std::nullopt;
    if (!worldFileExists(nextX, nextY)) return std::nullopt;

    const auto path = worldFilePath(nextX, nextY);
    auto loaded = loadMapFile(path);
    if (loaded.kind != "world") throw std::runtime_error(path.filename().string() + " is not a world map");
    requireKnownTiles(currentMap_, loaded, path);
    if (cell.x < 0) landX = loaded.width - 1;
    else if (cell.x >= current_.width) landX = 0;
    if (cell.y < 0) landY = loaded.height - 1;
    else if (cell.y >= current_.height) landY = 0;
    if (landX < 0 || landY < 0 || landX >= loaded.width || landY >= loaded.height) return std::nullopt;
    const auto found = currentMap_.find(loaded.tiles[static_cast<std::size_t>(landY)][static_cast<std::size_t>(landX)]);
    if (found == currentMap_.end() || !found->second.canEnter) return std::nullopt;

    MapData map;
    map.name = std::move(loaded.name);
    map.width = loaded.width;
    map.height = loaded.height;
    map.tiles = std::move(loaded.tiles);
    adopt(nextX, nextY, std::move(map));
    return Cell{landX, landY};
}
void MapManger::follow(Cell cell)
{
    focus_ = cell;
    refreshView();
}
void MapManger::refreshView()
{
    view_.origin = {focus_.x - config_.viewColumns / 2, focus_.y - config_.viewRows / 2};
    view_.tiles.assign(config_.viewRows, std::vector<std::string>(static_cast<std::size_t>(config_.viewColumns)));
    for (int y = 0; y < config_.viewRows; ++y)
        for (int x = 0; x < config_.viewColumns; ++x)
        {
            const int mapX = view_.origin.x + x, mapY = view_.origin.y + y;
            if (mapX < 0 || mapY < 0 || mapX >= current_.width || mapY >= current_.height) continue;
            view_.tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = current_.tiles[static_cast<std::size_t>(mapY)][static_cast<std::size_t>(mapX)];
        }
}
const std::string& MapManger::tileAt(int x, int y) const noexcept
{
    static const std::string outside;
    if (y < 0 || x < 0 || y >= current_.height || x >= current_.width) return outside;
    return current_.tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
}
bool MapManger::walkable(Cell cell) const noexcept
{
    const auto found = currentMap_.find(tileAt(cell.x, cell.y));
    return found != currentMap_.end() && found->second.canEnter;
}
const TileInfo& MapManger::tile(const std::string& id) const
{
    const auto found = currentMap_.find(id);
    if (found == currentMap_.end()) throw std::out_of_range("unknown tile " + id);
    return found->second;
}
void MapManger::loadTileInfo()
{
    const auto file = mapDir_ / "map_info.json";
    try
    {
        std::ifstream in(file);
        if (!in) throw std::runtime_error("failed to open " + file.generic_string());
        const auto root = nlohmann::json::parse(in);
        if (!root.is_object()) throw std::runtime_error("needs an object");
        for (const auto& field : root.items())
            if (field.key() != "globalConfig" && field.key() != "tiles")
                throw std::runtime_error("unknown key " + field.key());
        if (!root.contains("globalConfig") || !root["globalConfig"].is_object())
            throw std::runtime_error("needs globalConfig");
        const auto& config = root["globalConfig"];
        for (const auto& field : config.items())
            if (field.key() != "viewColumns" && field.key() != "viewRows" && field.key() != "tileSize"
                && field.key() != "marginX" && field.key() != "marginY" && field.key() != "framerate"
                && field.key() != "windowTitle" && field.key() != "backgroundColor"
                && field.key() != "emptyTileColor")
                throw std::runtime_error("globalConfig has unknown key " + field.key());
        auto requirePositive = [](const nlohmann::json& object, const char* key)
        {
            if (!object.contains(key) || !object[key].is_number_integer())
                throw std::runtime_error(std::string(key) + " must be an integer");
            const int size = object[key].get<int>();
            if (size <= 0) throw std::runtime_error(std::string(key) + " must be positive");
            return size;
        };
        config_.viewColumns = requirePositive(config, "viewColumns");
        config_.viewRows = requirePositive(config, "viewRows");
        config_.tileSize = requireNumber(config, "tileSize");
        config_.margin = {requireNumber(config, "marginX"), requireNumber(config, "marginY")};
        config_.framerate = requirePositive(config, "framerate");
        config_.windowTitle = requireText(config, "globalConfig", "windowTitle");
        config_.background = requireColor(config, "backgroundColor");
        config_.emptyTile = requireColor(config, "emptyTileColor");
        if (!root.contains("tiles") || !root["tiles"].is_array()) throw std::runtime_error("needs tiles");
        for (const auto& value : root["tiles"])
        {
            if (!value.is_object()) throw std::runtime_error("tile needs an object");
            for (const auto& field : value.items())
                if (field.key() != "id" && field.key() != "name" && field.key() != "image" && field.key() != "canEnter")
                    throw std::runtime_error("tile has unknown key " + field.key());
            const auto id = requireText(value, "tile", "id");
            if (!isTileId(id)) throw std::runtime_error(id + " is not a numeric tile id");
            if (currentMap_.contains(id)) throw std::runtime_error("repeats tile " + id);
            if (!value.contains("canEnter") || !value["canEnter"].is_boolean())
                throw std::runtime_error(id + " needs name, image and canEnter");
            auto imagePath = std::filesystem::path(requireText(value, id, "image"));
            if (imagePath.is_relative()) imagePath = mapDir_ / imagePath;
            if (!std::filesystem::is_regular_file(imagePath))
                throw std::runtime_error(id + " image not found: " + imagePath.generic_string());
            TileInfo info;
            info.name = requireText(value, id, "name");
            info.image = std::move(imagePath);
            info.canEnter = value["canEnter"].get<bool>();
            currentMap_.emplace(id, std::move(info));
        }
        if (currentMap_.empty()) throw std::runtime_error("has no tiles");
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(file.generic_string() + ": " + error.what());
    }
}
Cell MapManger::entryPoint() const
{
    const Cell point{2, 7};
    if (!walkable(point)) throw std::logic_error("Map entry point is blocked");
    return point;
}
}
