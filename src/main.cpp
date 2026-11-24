#include "Game.hpp"
#include "Manager/MapFile.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
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

	float jsonPositive(const nlohmann::json& object, const char* key)
	{
		if (!object.contains(key) || !object[key].is_number())
			throw std::runtime_error(std::string(key) + " must be a number");
		const float value = object[key].get<float>();
		if (value <= 0.f) throw std::runtime_error(std::string(key) + " must be positive");
		return value;
	}

	maptool::Color jsonColor(const nlohmann::json& object, const char* key)
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
		return { channels[0], channels[1], channels[2] };
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
			const auto candidate = dir / "src" / "self_test.json";
			if (std::filesystem::is_regular_file(candidate)) return candidate.parent_path();
			if (dir.parent_path() == dir) throw std::runtime_error("src/self_test.json not found");
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
		const auto src = sourceDirectory();
		const auto root = loadJson(src / "self_test.json");
		const auto game = loadJson(src / "game_config.json");
		requireKeys(root, { "project", "viewShift", "startMap" });
		requireKeys(root["viewShift"], { "x", "y" });
		requireKeys(root["startMap"], { "chapter", "x", "y" });
		requireKeys(game, { "character" });
		requireKeys(game["character"], { "radiusScale", "color" });
		maptool::GameSettings settings;
		settings.project = jsonText(root, "project");
		settings.viewShift = { jsonInt(root["viewShift"], "x"), jsonInt(root["viewShift"], "y") };
		settings.chapter = jsonInt(root["startMap"], "chapter");
		settings.startMap = { jsonInt(root["startMap"], "x"), jsonInt(root["startMap"], "y") };
		settings.characterRadiusScale = jsonPositive(game["character"], "radiusScale");
		settings.characterColor = jsonColor(game["character"], "color");
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
