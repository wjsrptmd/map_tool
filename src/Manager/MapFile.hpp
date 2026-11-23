#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace maptool
{
struct MapFile
{
    std::string kind, name;
    int width = 0, height = 0;
    std::vector<std::vector<std::string>> tiles;
};

struct WorldMapFile
{
    std::filesystem::path path;
    int x = 0;
    int y = 0;
};

MapFile loadMapFile(const std::filesystem::path& file);
std::filesystem::path projectDirectory();
std::vector<WorldMapFile> worldMapFiles(const std::filesystem::path& dir);
}
