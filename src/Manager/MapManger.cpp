#include "MapManger.hpp"
#include "../model/MapFile.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <thread>
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

std::optional<Cell> landingCell(Cell cell, int currentWidth, int currentHeight, int nextWidth, int nextHeight)
{
    int landX = cell.x;
    int landY = cell.y;
    if (cell.x < 0) landX = nextWidth - 1;
    else if (cell.x >= currentWidth) landX = 0;
    if (cell.y < 0) landY = nextHeight - 1;
    else if (cell.y >= currentHeight) landY = 0;
    if (landX < 0 || landY < 0 || landX >= nextWidth || landY >= nextHeight) return std::nullopt;
    return Cell{landX, landY};
}
}

MapManger::MapManger()
{
    const auto project = projectDirectory();
    projectName_ = project.filename().string();
    mapDir_ = project / "maps";
    std::error_code error;
    if (!std::filesystem::is_directory(mapDir_, error))
        throw std::runtime_error("maps not found: " + mapDir_.generic_string());
    loadTileInfo();
}
void MapManger::loadChapter(int chapter)
{
    if (chapter <= 0) throw std::invalid_argument("chapter must be positive");
    if (pending_.valid()) throw std::runtime_error("cannot load a chapter while a map is loading");
    const auto dir = mapDir_ / "world" / ("chapter" + std::to_string(chapter));
    std::error_code error;
    if (!std::filesystem::is_directory(dir, error))
        throw std::runtime_error("chapter not found: " + dir.generic_string());
    const auto worldFiles = worldMapFiles(dir);
    for (const auto& file : worldFiles)
    {
        auto map = loadMapFile(file.path);
        if (map.kind != "world") throw std::runtime_error(file.path.filename().string() + " is not a world map");
        requireKnownTiles(currentMap_, map, file.path);
    }
    chapter_ = chapter;
    setCurrentMap(worldFiles.front().x, worldFiles.front().y);
}
bool MapManger::worldFileExists(int x, int y) const noexcept
{
    std::error_code error;
    return std::filesystem::is_regular_file(worldFilePath(x, y), error);
}
std::filesystem::path MapManger::worldFilePath(int x, int y) const
{
    return mapDir_ / "world" / ("chapter" + std::to_string(chapter_)) / (std::to_string(y) + "_" + std::to_string(x) + ".map");
}
MapData MapManger::loadWorld(int x, int y) const
{
    const auto path = worldFilePath(x, y);
    auto loaded = loadMapFile(path);
    if (loaded.kind != "world") throw std::runtime_error(path.filename().string() + " is not a world map");
    requireKnownTiles(currentMap_, loaded, path);
    MapData map;
    map.name = std::move(loaded.name);
    map.width = loaded.width;
    map.height = loaded.height;
    map.tiles = std::move(loaded.tiles);
    return map;
}
void MapManger::preloadNeighbors(int x, int y)
{
    const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& step : steps)
    {
        const int nextX = x + step[0];
        const int nextY = y + step[1];
        const MapCoord key{nextX, nextY};
        if (loaded_.contains(key) || !worldFileExists(nextX, nextY)) continue;
        loaded_.emplace(key, loadWorld(nextX, nextY));
    }
}
void MapManger::setCurrentMap(int x, int y)
{
    if (!worldFileExists(x, y)) throw std::out_of_range("World map does not exist");
    const MapCoord key{x, y};
    if (const auto found = loaded_.find(key); found != loaded_.end())
        adopt(x, y, found->second);
    else
        adopt(x, y, loadWorld(x, y));
}
void MapManger::adopt(int x, int y, MapData map)
{
    loaded_.insert_or_assign(MapCoord{x, y}, map);
    current_ = std::move(map);
    currentX_ = x;
    currentY_ = y;
    focus_ = {};
    refreshView();
    preloadNeighbors(x, y);
}
bool MapManger::loading() const noexcept
{
    return pending_.valid();
}
std::optional<MapManger::PendingMap> MapManger::readNeighbor(std::filesystem::path path, std::map<std::string, bool> canEnter,
    int currentWidth, int currentHeight, Cell cell, int nextX, int nextY)
{
    auto loaded = loadMapFile(path);
    if (loaded.kind != "world") throw std::runtime_error(path.filename().string() + " is not a world map");
    for (const auto& row : loaded.tiles)
        for (const auto& id : row)
            if (!canEnter.contains(id))
                throw std::runtime_error(path.filename().string() + " has unknown tile " + id);
    const auto landing = landingCell(cell, currentWidth, currentHeight, loaded.width, loaded.height);
    if (!landing) return std::nullopt;
    const auto found = canEnter.find(loaded.tiles[static_cast<std::size_t>(landing->y)][static_cast<std::size_t>(landing->x)]);
    if (found == canEnter.end() || !found->second) return std::nullopt;

    PendingMap pending;
    pending.x = nextX;
    pending.y = nextY;
    pending.landing = *landing;
    pending.map.name = std::move(loaded.name);
    pending.map.width = loaded.width;
    pending.map.height = loaded.height;
    pending.map.tiles = std::move(loaded.tiles);
    return pending;
}
bool MapManger::beginNeighbor(Cell cell)
{
    if (pending_.valid()) return false;
    const bool insideX = cell.x >= 0 && cell.x < current_.width;
    const bool insideY = cell.y >= 0 && cell.y < current_.height;
    int nextX = currentX_;
    int nextY = currentY_;
    if (insideX && cell.y < 0)
        --nextY;
    else if (insideX && cell.y >= current_.height)
        ++nextY;
    else if (insideY && cell.x < 0)
        --nextX;
    else if (insideY && cell.x >= current_.width)
        ++nextX;
    else
        return false;
    if (!worldFileExists(nextX, nextY)) return false;

    if (const auto found = loaded_.find(MapCoord{nextX, nextY}); found != loaded_.end())
    {
        const auto landing = landingCell(cell, current_.width, current_.height, found->second.width, found->second.height);
        if (!landing) return false;
        const auto& id = found->second.tiles[static_cast<std::size_t>(landing->y)][static_cast<std::size_t>(landing->x)];
        const auto info = currentMap_.find(id);
        if (info == currentMap_.end() || !info->second.canEnter) return false;
        PendingMap ready;
        ready.x = nextX;
        ready.y = nextY;
        ready.landing = *landing;
        ready.map = found->second;
        std::promise<std::optional<PendingMap>> promise;
        auto future = promise.get_future();
        promise.set_value(std::move(ready));
        pending_ = std::move(future);
        return true;
    }

    std::map<std::string, bool> canEnter;
    for (const auto& [id, info] : currentMap_)
        canEnter.emplace(id, info.canEnter);
    const auto path = worldFilePath(nextX, nextY);
    const int width = current_.width;
    const int height = current_.height;
    pending_ = std::async(std::launch::async, [path, canEnter, width, height, cell, nextX, nextY] {
        return readNeighbor(path, canEnter, width, height, cell, nextX, nextY);
    });
    return true;
}
std::optional<Cell> MapManger::pollNeighbor()
{
    if (!pending_.valid()) return std::nullopt;
    if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    auto loaded = pending_.get();
    if (!loaded) return std::nullopt;
    const Cell landing = loaded->landing;
    adopt(loaded->x, loaded->y, std::move(loaded->map));
    return landing;
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
}
