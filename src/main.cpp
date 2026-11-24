#include "Game.hpp"
#include "model/MapFile.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	int jsonInt(const nlohmann::json& object, const char* key)
	{
		if (!object.contains(key) || !object[key].is_number_integer())
			throw std::runtime_error(std::string(key) + " must be an integer");
		return object[key].get<int>();
	}

	std::string jsonText(const nlohmann::json& object, const char* key)
	{
		if (!object.contains(key) || !object[key].is_string() || object[key].get<std::string>().empty())
			throw std::runtime_error(std::string(key) + " must be text");
		return object[key].get<std::string>();
	}

	void requireKeys(const nlohmann::json& object, std::initializer_list<const char*> keys)
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

	std::filesystem::path sourceDirectory()
	{
		for (auto dir = maptool::projectDirectory();; dir = dir.parent_path())
		{
			const auto candidate = dir / "src" / "game_config.json";
			if (std::filesystem::is_regular_file(candidate)) return candidate.parent_path();
			if (dir.parent_path() == dir) throw std::runtime_error("src/game_config.json not found");
		}
	}

	nlohmann::json loadJson(const std::filesystem::path& file)
	{
		std::ifstream in(file);
		if (!in) throw std::runtime_error("failed to open " + file.generic_string());
		return nlohmann::json::parse(in);
	}

	maptool::GameSettings readSettings()
	{
		const auto root = loadJson(sourceDirectory() / "game_config.json");
		requireKeys(root, { "project", "viewShift", "startMap", "character" });
		requireKeys(root["viewShift"], { "x", "y" });
		requireKeys(root["startMap"], { "chapter", "x", "y" });
		requireKeys(root["character"], { "id", "moveInterval" });
		maptool::GameSettings settings;
		settings.project = jsonText(root, "project");
		settings.viewShift = { jsonInt(root["viewShift"], "x"), jsonInt(root["viewShift"], "y") };
		settings.chapter = jsonInt(root["startMap"], "chapter");
		settings.startMap = { jsonInt(root["startMap"], "x"), jsonInt(root["startMap"], "y") };
		settings.characterId = jsonText(root["character"], "id");
		settings.moveInterval = jsonInt(root["character"], "moveInterval");
		if (settings.moveInterval <= 0) throw std::runtime_error("moveInterval must be positive");
		return settings;
	}
}

int main(int argc, char** argv)
{
	try
	{
		const auto settings = readSettings();
		maptool::Game game;
		const bool selfTest = argc > 1 && std::string(argv[1]) == "--self-test";
		return game.run(settings, selfTest);
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
