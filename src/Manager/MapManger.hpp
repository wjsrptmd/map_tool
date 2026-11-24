#pragma once
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace maptool
{
struct Cell
{
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(Cell left, Cell right) noexcept
    {
        return left.x == right.x && left.y == right.y;
    }
    friend constexpr bool operator!=(Cell left, Cell right) noexcept { return !(left == right); }
    friend constexpr Cell operator+(Cell left, Cell right) noexcept { return {left.x + right.x, left.y + right.y}; }
    friend constexpr Cell operator-(Cell left, Cell right) noexcept { return {left.x - right.x, left.y - right.y}; }
};

struct Vec2
{
    float x = 0.f;
    float y = 0.f;
};

struct Color
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// Display settings from map_info.json globalConfig.
struct GlobalConfig
{
    int viewColumns = 0;
    int viewRows = 0;
    float tileSize = 0.f;
    Vec2 margin;
    int framerate = 0;
    std::string windowTitle;
    Color background;
    Color emptyTile;
};

// One tile from the selected project's map_info.json.
struct TileInfo
{
    std::string name;
    std::filesystem::path image;
    bool canEnter = false;
};

// The loaded map. Each cell stores a tile id from map_info.json.
struct MapData
{
    std::string name;
    int width = 0, height = 0;
    std::vector<std::vector<std::string>> tiles;
};

// The fixed grid shown to the player. origin is the map cell at the view's top-left.
struct ViewMap
{
    Cell origin;
    std::vector<std::vector<std::string>> tiles;
};

// Owns the selected project's tile info, the current world map, and the adjacent maps kept ready for travel.
class MapManger
{
public:
    MapManger();

    MapManger(const MapManger&) = delete;
    MapManger& operator=(const MapManger&) = delete;
    MapManger(MapManger&&) = delete;
    MapManger& operator=(MapManger&&) = delete;

    const GlobalConfig& config() const noexcept { return config_; }
    int viewColumns() const noexcept { return config_.viewColumns; }
    int viewRows() const noexcept { return config_.viewRows; }
    const std::string& projectName() const noexcept { return projectName_; }
    const TileInfo& tile(const std::string& id) const;
    void loadChapter(int chapter);
    void setCurrentMap(int x, int y);
    void follow(Cell cell);
    const MapData& current() const noexcept { return current_; }
    const ViewMap& view() const noexcept { return view_; }
    int currentChapter() const noexcept { return chapter_; }
    int currentMapX() const noexcept { return currentX_; }
    int currentMapY() const noexcept { return currentY_; }
    const std::string& tileAt(int x, int y) const noexcept;
    bool walkable(Cell cell) const noexcept;
    bool loading() const noexcept;
    bool beginNeighbor(Cell cell);
    std::optional<Cell> pollNeighbor();

private:
    struct PendingMap
    {
        int x = 0;
        int y = 0;
        Cell landing;
        MapData map;
    };

    struct MapCoord
    {
        int x = 0;
        int y = 0;

        friend bool operator<(MapCoord left, MapCoord right) noexcept
        {
            return left.y < right.y || (left.y == right.y && left.x < right.x);
        }
    };

    static std::optional<PendingMap> readNeighbor(std::filesystem::path path, std::map<std::string, bool> canEnter,
        int currentWidth, int currentHeight, Cell cell, int nextX, int nextY);
    bool worldFileExists(int x, int y) const noexcept;
    std::filesystem::path worldFilePath(int x, int y) const;
    MapData loadWorld(int x, int y) const;
    void preloadNeighbors(int x, int y);
    void loadTileInfo();
    void refreshView();
    void adopt(int x, int y, MapData map);

    std::filesystem::path mapDir_;
    std::string projectName_;
    MapData current_;
    ViewMap view_;
    Cell focus_;
    std::map<std::string, TileInfo> currentMap_;
    GlobalConfig config_;
    int chapter_ = 0;
    int currentX_ = 0, currentY_ = 0;
    std::map<MapCoord, MapData> loaded_;
    std::future<std::optional<PendingMap>> pending_;
};
}
