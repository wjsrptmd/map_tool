#include "Game.hpp"
#include "util/Log.hpp"
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
			maptool::fail(std::string(key) + " must be an integer");
		return object[key].get<int>();
	}

	float jsonFloat(const nlohmann::json& object, const char* key)
	{
		if (!object.contains(key) || !object[key].is_number())
			maptool::fail(std::string(key) + " must be a positive number");
		return object[key].get<float>();
	}

	std::string jsonText(const nlohmann::json& object, const char* key)
	{
		if (!object.contains(key) || !object[key].is_string() || object[key].get<std::string>().empty())
			maptool::fail(std::string(key) + " must be text");
		return object[key].get<std::string>();
	}

	void requireKeys(const nlohmann::json& object, std::initializer_list<const char*> keys)
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

	std::filesystem::path sourceDirectory()
	{
		for (auto dir = maptool::projectDirectory();; dir = dir.parent_path())
		{
			const auto candidate = dir / "src" / "game_config.json";
			if (std::filesystem::is_regular_file(candidate)) return candidate.parent_path();
			if (dir.parent_path() == dir) maptool::fail("src/game_config.json not found");
		}
	}

	nlohmann::json loadJson(const std::filesystem::path& file)
	{
		std::ifstream in(file);
		if (!in) maptool::fail("failed to open " + file.generic_string());
		return nlohmann::json::parse(in);
	}

	maptool::GameSettings readSettings()
	{
		const auto root = loadJson(sourceDirectory() / "game_config.json");
		requireKeys(root, { "debugMode", "project", "viewShift", "startMap", "character" });
		requireKeys(root["viewShift"], { "x", "y" });
		requireKeys(root["startMap"], { "chapter", "x", "y" });
		requireKeys(root["character"], { "id", "speed" });
		maptool::GameSettings settings;
		if (!root["debugMode"].is_boolean()) maptool::fail("debugMode must be a boolean");
		settings.debugMode = root["debugMode"].get<bool>();
		settings.project = jsonText(root, "project");
		settings.viewShift = { jsonInt(root["viewShift"], "x"), jsonInt(root["viewShift"], "y") };
		settings.chapter = jsonInt(root["startMap"], "chapter");
		settings.startMap = { jsonInt(root["startMap"], "x"), jsonInt(root["startMap"], "y") };
		settings.characterId = jsonText(root["character"], "id");
		settings.speed = jsonFloat(root["character"], "speed");
		if (!(settings.speed > 0.f)) maptool::fail("speed must be a positive number");
		return settings;
	}
}

int main(int argc, char** argv)
{
	try
	{
		const auto settings = readSettings();
		if (settings.debugMode) maptool::Log::open(sourceDirectory().parent_path() / "logs");
		maptool::Log::info("---------- program start ----------");
		maptool::Log::info("settings project " + settings.project
			+ " chapter " + std::to_string(settings.chapter)
			+ " map " + std::to_string(settings.startMap.x) + "," + std::to_string(settings.startMap.y)
			+ " character " + settings.characterId
			+ " speed " + std::to_string(settings.speed)
			+ (settings.debugMode ? " debugMode on" : " debugMode off"));
		maptool::Game game;
		const bool selfTest = argc > 1 && std::string(argv[1]) == "--self-test";
		maptool::Log::info(selfTest ? "run self-test" : "run play");
		return game.run(settings, selfTest);
	}
	catch (const std::exception& error)
	{
		try { maptool::Log::error(std::string("uncaught exception: ") + error.what()); }
		catch (...) {}
		std::cerr << error.what() << '\n';
		return 1;
	}
}
