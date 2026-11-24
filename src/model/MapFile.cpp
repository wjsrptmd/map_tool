#include "MapFile.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

namespace maptool
{
	namespace
	{
		std::string trim(std::string text)
		{
			const auto first = text.find_first_not_of(" \t\r");
			if (first == std::string::npos) return {};
			const auto last = text.find_last_not_of(" \t\r");
			return text.substr(first, last - first + 1);
		}

		int readInt(const std::string& text, const std::string& where)
		{
			std::size_t used = 0;
			int value = 0;
			try { value = std::stoi(text, &used); }
			catch (const std::exception&) { throw std::runtime_error(where + " is not an integer"); }
			if (used != text.size()) throw std::runtime_error(where + " is not an integer");
			return value;
		}

		bool isTileId(const std::string& id)
		{
			if (id.empty()) return false;
			for (const unsigned char c : id)
				if (c < '0' || c > '9') return false;
			return true;
		}

		std::filesystem::path repositoryRoot()
		{
			auto dir = std::filesystem::current_path();
			const auto start = dir;
			for (;;)
			{
				if (std::filesystem::is_regular_file(dir / "config" / "start_config.json")) return dir;
				const auto parent = dir.parent_path();
				if (parent == dir) break;
				dir = parent;
			}
			throw std::runtime_error("config/start_config.json not found from " + start.generic_string());
		}

	}

	std::filesystem::path projectDirectory()
	{
		const auto dir = repositoryRoot() / "resources";
		if (!std::filesystem::is_directory(dir))
			throw std::runtime_error("resources not found: " + dir.generic_string());
		return dir;
	}

	MapFile loadMapFile(const std::filesystem::path& file)
	{
		std::ifstream in(file);
		if (!in) throw std::runtime_error("failed to open " + file.string());
		const auto where = file.filename().string();
		MapFile map;
		bool seenKind = false, seenName = false, seenWidth = false, seenHeight = false, inTiles = false;
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && static_cast<unsigned char>(line.front()) == 0xEF)
				line.erase(0, line.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0);
			if (inTiles)
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty()) continue;
				std::vector<std::string> row;
				std::size_t start = 0;
				while (start <= line.size())
				{
					const auto next = line.find('-', start);
					const auto token = line.substr(start, next == std::string::npos ? std::string::npos : next - start);
					if (!isTileId(token)) throw std::runtime_error(where + " has a tile id that is not digits");
					row.push_back(token);
					if (next == std::string::npos) break;
					start = next + 1;
				}
				map.tiles.push_back(std::move(row));
				continue;
			}
			line = trim(std::move(line));
			if (line.empty() || line.front() == ';' || line.front() == '#') continue;
			if (line == "-") { inTiles = true; continue; }
			const auto dash = line.find('-');
			if (dash == std::string::npos) throw std::runtime_error(where + " has a header line without '-'");
			const auto key = trim(line.substr(0, dash));
			const auto value = trim(line.substr(dash + 1));
			if (key == "kind")
			{
				if (seenKind) throw std::runtime_error(where + " repeats kind");
				if (value != "world") throw std::runtime_error(where + " has unknown kind " + value);
				map.kind = value; seenKind = true;
			}
			else if (key == "name")
			{
				if (seenName) throw std::runtime_error(where + " repeats name");
				if (value.empty()) throw std::runtime_error(where + " has an empty name");
				map.name = value; seenName = true;
			}
			else if (key == "width" || key == "height")
			{
				auto& seen = key == "width" ? seenWidth : seenHeight;
				if (seen) throw std::runtime_error(where + " repeats " + key);
				const int size = readInt(value, where + " " + key);
				if (size <= 0) throw std::runtime_error(where + " has a non-positive " + key);
				(key == "width" ? map.width : map.height) = size;
				seen = true;
			}
			else throw std::runtime_error(where + " has unknown key " + key);
		}
		if (!seenKind || !seenName || !seenWidth || !seenHeight)
			throw std::runtime_error(where + " needs kind, name, width and height");
		if (map.tiles.empty() || map.tiles.front().empty()) throw std::runtime_error(where + " has no tiles");
		const auto width = map.tiles.front().size();
		for (const auto& row : map.tiles)
			if (row.size() != width) throw std::runtime_error(where + " has rows of different widths");
		const auto height = map.tiles.size();
		if (map.width != static_cast<int>(width) || map.height != static_cast<int>(height))
			throw std::runtime_error(where + " size does not match its tiles");
		return map;
	}

	bool readCoord(const std::string& text, int& value)
	{
		if (text.empty()) return false;
		std::size_t used = 0;
		try { value = std::stoi(text, &used); }
		catch (const std::exception&) { return false; }
		return used == text.size();
	}

	std::vector<WorldMapFile> worldMapFiles(const std::filesystem::path& dir)
	{
		if (!std::filesystem::is_directory(dir)) throw std::runtime_error(dir.generic_string() + " not found");
		std::vector<WorldMapFile> files;
		for (const auto& entry : std::filesystem::directory_iterator(dir))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".map") continue;
			const auto stem = entry.path().stem().string();
			const auto split = stem.find('_');
			int y = 0, x = 0;
			if (split == std::string::npos || stem.find('_', split + 1) != std::string::npos ||
				!readCoord(stem.substr(0, split), y) || !readCoord(stem.substr(split + 1), x))
				throw std::runtime_error(entry.path().filename().string() + " must be named y_x.map");
			files.push_back({ entry.path(), x, y });
		}
		if (files.empty()) throw std::runtime_error(dir.generic_string() + " has no .map files");
		std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
			return a.y < b.y || (a.y == b.y && a.x < b.x);
			});
		for (std::size_t i = 1; i < files.size(); ++i)
			if (files[i].x == files[i - 1].x && files[i].y == files[i - 1].y)
				throw std::runtime_error("duplicate world map coordinate " + std::to_string(files[i].y) + "_" + std::to_string(files[i].x));
		return files;
	}
}
