#include "Game.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include "util/Log.hpp"

namespace maptool
{
	Game::Game() : service_(maps_, characters_) {}


	const sf::Texture* Game::TilePictures::get(const MapManger& maps, const std::string& id)
	{
		if (id.empty()) return nullptr;
		const auto found = loaded_.find(id);
		if (found != loaded_.end()) return &found->second;
		sf::Texture texture(maps.tile(id).image);
		texture.setSmooth(true);
		return &loaded_.try_emplace(id, std::move(texture)).first->second;
	}

	sf::Color Game::toSf(Color color)
	{
		return { color.r, color.g, color.b };
	}

	Cell Game::directionStep(sf::Keyboard::Key key)
	{
		switch (key)
		{
		case sf::Keyboard::Key::Up: return { 0, -1 };
		case sf::Keyboard::Key::Down: return { 0, 1 };
		case sf::Keyboard::Key::Left: return { -1, 0 };
		case sf::Keyboard::Key::Right: return { 1, 0 };
		default: return {};
		}
	}

	void Game::drawTile(sf::RenderTarget& out, const sf::Texture* texture, sf::Vector2f position) const
	{
		const auto& config = maps_.config();
		const float tile = config.tileSize;
		if (texture == nullptr || texture->getSize().x == 0 || texture->getSize().y == 0)
		{
			sf::RectangleShape empty({ tile, tile });
			empty.setPosition(position);
			empty.setFillColor(toSf(config.emptyTile));
			out.draw(empty);
			return;
		}
		sf::Sprite sprite(*texture);
		const auto size = texture->getSize();
		sprite.setScale({ tile / static_cast<float>(size.x), tile / static_cast<float>(size.y) });
		sprite.setPosition(position);
		out.draw(sprite);
	}

	const sf::Texture& Game::spriteTexture(const Animation& animation)
	{
		const auto key = animation.image.generic_string();
		if (const auto found = sprites_.find(key); found != sprites_.end()) return found->second;
		sf::Texture texture(animation.image);
		const auto& sheet = characters_.config();
		texture.setSmooth(sheet.textureSmooth);
		const auto size = texture.getSize();
		const auto width = static_cast<unsigned>(sheet.frameWidth * animation.frames);
		const auto height = static_cast<unsigned>(sheet.frameHeight);
		if (size.x != width || size.y != height)
			maptool::fail(animation.image.generic_string() + " has the wrong size " + std::to_string(size.x) + "x" + std::to_string(size.y)
				+ ", expected " + std::to_string(width) + "x" + std::to_string(height));
		Log::info("loaded sprite " + key + " " + std::to_string(size.x) + "x" + std::to_string(size.y));
		return sprites_.emplace(key, std::move(texture)).first->second;
	}

	void Game::drawGame(sf::RenderTarget& out)
	{
		const auto& config = maps_.config();
		const auto& view = maps_.view();
		const sf::Vector2f margin{ config.margin.x, config.margin.y };
		for (int y = 0; y < maps_.viewRows(); ++y)
			for (int x = 0; x < maps_.viewColumns(); ++x)
			{
				const auto& id = view.tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
				const auto position = margin + sf::Vector2f{ x * config.tileSize, y * config.tileSize };
				drawTile(out, pictures_.get(maps_, id), position);
			}
		const auto& sheet = characters_.config();
		for (const auto& actor : characters_.actors())
		{
			if (actor.chapter != maps_.currentChapter() || actor.mapX != maps_.currentMapX() || actor.mapY != maps_.currentMapY())
				continue;
			const auto& body = actor.body;
			const auto& animation = body.animation(body.state, body.direction);
			const Cell local{ actor.x - view.origin.x, actor.y - view.origin.y };
			sf::Sprite sprite(spriteTexture(animation), sf::IntRect({ body.frame() * sheet.frameWidth, 0 }, { sheet.frameWidth, sheet.frameHeight }));
			sprite.setOrigin({ body.anchor.x, body.anchor.y });
			sprite.setPosition(margin + sf::Vector2f{ (local.x + .5f) * config.tileSize, (local.y + .5f) * config.tileSize });
			out.draw(sprite);
		}
	}

	void Game::drawLoading(sf::RenderTarget& out) const
	{
		const auto size = sf::Vector2f(out.getSize());
		sf::RectangleShape bar({ size.x * 0.35f, 18.f });
		bar.setOrigin(bar.getSize() * 0.5f);
		bar.setPosition(size * 0.5f);
		bar.setFillColor({ 232, 216, 176 });
		out.draw(bar);
	}

	void Game::moveCharacter(Cell step)
	{
		const auto before = service_.playerCell();
		service_.movePlayer(step);
		if (service_.playerCell() != before)
			maps_.follow(camera_.focus(maps_, service_.playerCell()));
	}

	void Game::openChapter(const GameSettings& settings)
	{
		Log::info("open chapter " + std::to_string(settings.chapter)
			+ " map " + std::to_string(settings.startMap.x) + "," + std::to_string(settings.startMap.y)
			+ " character " + settings.characterId);
		maps_.loadChapter(settings.chapter);
		maps_.setCurrentMap(settings.startMap.x, settings.startMap.y);
		service_.placePlayer(settings.characterId, settings.chapter, settings.startMap);
	}

	bool Game::checkPlacements(const GameSettings& settings)
	{
		const Cell home{2, 8};
		const int player = service_.player();
		if (service_.playerCell() != home) return false;
		const auto& body = characters_.actor(player).body;
		if (body.id != settings.characterId || body.state != "idle" || body.direction != "right") return false;
		if (!characters_.actor(player).player) return false;

		auto countKeeper = [&]() {
			int npcs = 0;
			for (const auto& actor : characters_.actors())
			{
				if (actor.player)
				{
					if (actor.body.id != settings.characterId) return -1;
					continue;
				}
				++npcs;
				if (actor.instanceId != "ch1_west_gate_keeper_0001" || actor.body.id != "00000002") return -1;
				if (actor.x != 5 || actor.y != 7 || actor.body.state != "idle" || actor.body.direction != "down") return -1;
				if (!actor.collision || actor.interactionId != "ch1_west_gate_keeper") return -1;
				if (actor.chapter != settings.chapter || actor.mapX != settings.startMap.x || actor.mapY != settings.startMap.y) return -1;
			}
			return npcs;
		};
		if (countKeeper() != 1) return false;
		service_.enterMap();
		if (countKeeper() != 1) return false;

		const Cell keeper{5, 7};
		Cell beside{};
		bool besideFound = false;
		const Cell around[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		for (const Cell step : around)
		{
			const Cell cell = keeper + step;
			if (!maps_.walkable(cell)) continue;
			beside = cell;
			besideFound = true;
			break;
		}
		if (!besideFound) return false;
		characters_.move(player, beside.x, beside.y);
		service_.movePlayer(keeper - beside);
		if (service_.playerCell() != beside || characters_.at(player).state != "idle") return false;

		maps_.setCurrentMap(settings.startMap.x + 1, settings.startMap.y);
		Cell landing{};
		bool landingFound = false;
		for (int y = 0; y < maps_.current().height && !landingFound; ++y)
			for (int x = 0; x < maps_.current().width && !landingFound; ++x)
				if (maps_.walkable({x, y}))
				{
					landing = {x, y};
					landingFound = true;
				}
		if (!landingFound) return false;
		service_.arrive(landing);
		if (service_.playerCell() != landing) return false;
		for (const auto& actor : characters_.actors())
			if (!actor.player || actor.instanceId == "ch1_west_gate_keeper_0001") return false;
		maps_.setCurrentMap(settings.startMap.x, settings.startMap.y);
		service_.arrive(home);
		characters_.at(player).idle("right");
		if (service_.playerCell() != home || countKeeper() != 1) return false;

		const std::string label = "placement-test";
		auto actorJson = [](const std::string& instance, const std::string& character, int x, int y) {
			return std::string("{\"instanceId\":\"") + instance + "\",\"characterId\":\"" + character
				+ "\",\"position\":{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y)
				+ "},\"direction\":\"down\",\"state\":\"idle\",\"collision\":true,\"enabled\":true,\"interactionId\":\"talk\"}";
		};
		auto document = [&](const std::string& key, const std::string& actors, int x = 2, int y = 8) {
			return std::string("{\"") + key + "\":{\"playerStart\":{\"position\":{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y)
				+ "},\"direction\":\"right\"},\"maps\":[{\"map\":{\"x\":0,\"y\":0},\"actors\":[" + actors + "]}]}}";
		};
		auto rejects = [&](const std::string& json, const std::string& field) {
			try
			{
				service_.validatePlacement(json, label, settings.chapter, settings.startMap, settings.characterId);
				return false;
			}
			catch (const std::exception& error)
			{
				const std::string text = error.what();
				return text.find(label) != std::string::npos && text.find(field) != std::string::npos;
			}
		};
		if (!rejects("{\"chapter01\":{}}", "chapter01 is not a chapter")) return false;
		if (!rejects("{}", "chapter1 is required")) return false;
		if (!rejects(document("chapter1", actorJson("same", settings.characterId, 2, 8) + "," + actorJson("same", "00000002", 3, 8)), "instanceId repeats")) return false;
		if (!rejects(document("chapter1", actorJson("one", "00000099", 3, 8)), "characterId is unknown")) return false;
		if (!rejects(document("chapter1", actorJson("one", "00000002", maps_.current().width, 0)), "is outside the map")) return false;
		bool blocked = false;
		for (int y = 0; y < maps_.current().height && !blocked; ++y)
			for (int x = 0; x < maps_.current().width && !blocked; ++x)
			{
				if (maps_.walkable({x, y})) continue;
				blocked = rejects(document("chapter1", actorJson("one", "00000002", x, y)), "is blocked");
			}
		if (!blocked) return false;
		if (!rejects(document("chapter1", actorJson("one", "00000002", 3, 8) + "," + actorJson("two", "00000002", 3, 8), 2, 8), "overlaps another character")) return false;
		if (maps_.currentChapter() != settings.chapter || maps_.currentMapX() != settings.startMap.x || maps_.currentMapY() != settings.startMap.y)
			return false;
		if (service_.playerCell() != home || countKeeper() != 1) return false;
		return true;
	}

	bool Game::checkNpcMovement()
	{
		const Cell home = service_.playerCell();
		const int player = service_.player();
		const std::string playerDirection = characters_.at(player).direction;
		if (characters_.movement(characters_.actor(player).body.id) != nullptr) return false;
		const CharacterManager::Actor* found = nullptr;
		for (const auto& actor : characters_.actors())
		{
			if (actor.player) continue;
			const MovementDefinition* move = characters_.movement(actor.body.id);
			if (move && move->route.size() >= 2)
			{
				found = &actor;
				break;
			}
		}
		if (!found) return false;
		const int instance = found->instance;
		const std::string instanceId = found->instanceId;
		const std::string characterId = found->body.id;
		const MovementDefinition definition = *characters_.movement(characterId);
		const float interval = 1.f / definition.speed;
		CharacterService::NpcCourse course;
		if (definition.route.size() < 4) return false;
		if (!service_.npcCourse(instanceId, course) || course.finished || course.routeIndex != 0 || course.position != course.origin)
			return false;
		const auto at = [&](std::size_t index) {
			const MovementStep step = definition.route[index];
			return Cell{course.origin.x + step.x, course.origin.y + step.y};
		};
		const MovementStep first = definition.route[1];
		const std::string facing = first.y < 0 ? "up" : first.y > 0 ? "down" : first.x < 0 ? "left" : "right";
		service_.advanceNpcs(interval * 0.25f);
		if (!service_.npcCourse(instanceId, course) || course.routeIndex != 0 || characters_.actor(instance).x != at(0).x || characters_.actor(instance).y != at(0).y)
			return false;
		if (service_.playerCell() != home) return false;
		service_.advanceNpcs(interval);
		if (!service_.npcCourse(instanceId, course) || course.routeIndex != 1 || characters_.actor(instance).x != at(1).x || characters_.actor(instance).y != at(1).y)
			return false;
		const auto& body = characters_.actor(instance).body;
		if (body.state != "walk" || body.direction != facing) return false;
		const Animation& walk = characters_.character(characterId).animation("walk", facing);
		const float cycle = static_cast<float>(walk.frames) / walk.fps;
		if (!(walk.fps > 0.f) || !(interval * 0.25f + cycle < interval)) return false;
		service_.advanceNpcs(cycle);
		if (!service_.npcCourse(instanceId, course) || course.routeIndex != 1 || characters_.actor(instance).x != at(1).x)
			return false;
		if (characters_.actor(instance).body.state != "idle" || characters_.actor(instance).body.direction != facing) return false;
		service_.advanceNpcs(interval * 5.f);
		if (!service_.npcCourse(instanceId, course) || course.routeIndex != 2 || characters_.actor(instance).x != at(2).x || characters_.actor(instance).y != at(2).y)
			return false;

		if (!service_.standable(at(3), instance)) return false;
		characters_.move(player, at(3).x, at(3).y);
		if (service_.standable(at(3), instance)) return false;
		service_.advanceNpcs(interval);
		if (!service_.npcCourse(instanceId, course) || course.routeIndex != 2 || characters_.actor(instance).x != at(2).x)
			return false;
		if (characters_.actor(instance).body.state != "idle") return false;
		characters_.move(player, home.x, home.y);
		service_.advanceNpcs(interval);
		if (!service_.npcCourse(instanceId, course) || course.routeIndex != 3 || characters_.actor(instance).x != at(3).x || characters_.actor(instance).y != at(3).y)
			return false;

		bool wall = false;
		for (int y = 0; y < maps_.current().height && !wall; ++y)
			for (int x = 0; x < maps_.current().width && !wall; ++x)
			{
				if (maps_.walkable({x, y})) continue;
				if (service_.standable({x, y}, instance)) return false;
				wall = true;
			}
		if (!wall) return false;
		if (service_.standable({-1, 0}, instance) || service_.standable({maps_.current().width, 0}, instance)) return false;

		CharacterService::NpcCourse saved;
		if (!service_.npcCourse(instanceId, saved)) return false;
		const int mapX = maps_.currentMapX();
		const int mapY = maps_.currentMapY();
		maps_.setCurrentMap(mapX + 1, mapY);
		service_.enterMap();
		for (const auto& actor : characters_.actors())
			if (actor.instanceId == instanceId) return false;
		service_.advanceNpcs(interval * 4.f);
		maps_.setCurrentMap(mapX, mapY);
		service_.enterMap();
		CharacterService::NpcCourse returned;
		if (!service_.npcCourse(instanceId, returned) || returned.position != saved.position || returned.routeIndex != saved.routeIndex || returned.finished != saved.finished)
			return false;
		bool restored = false;
		for (const auto& actor : characters_.actors())
		{
			if (actor.instanceId != instanceId) continue;
			restored = actor.x == saved.position.x && actor.y == saved.position.y;
		}
		if (!restored) return false;

		bool loading = false;
		const int width = maps_.current().width;
		const int height = maps_.current().height;
		for (int y = 0; y < height && !loading; ++y)
		{
			if (!maps_.walkable({width - 1, y})) continue;
			characters_.move(player, width - 1, y);
			service_.movePlayer({1, 0});
			loading = maps_.loading();
		}
		for (int x = 0; x < width && !loading; ++x)
		{
			if (!maps_.walkable({x, height - 1})) continue;
			characters_.move(player, x, height - 1);
			service_.movePlayer({0, 1});
			loading = maps_.loading();
		}
		if (!loading) return false;
		CharacterService::NpcCourse frozen;
		if (!service_.npcCourse(instanceId, frozen)) return false;
		service_.advanceNpcs(interval * 5.f);
		CharacterService::NpcCourse during;
		if (!service_.npcCourse(instanceId, during) || during.position != frozen.position || during.routeIndex != frozen.routeIndex)
			return false;
		maps_.pollNeighbor();
		maps_.setCurrentMap(mapX, mapY);
		service_.enterMap();
		characters_.move(player, home.x, home.y);
		characters_.at(player).idle(playerDirection);
		if (service_.playerCell() != home || maps_.currentMapX() != mapX || maps_.currentMapY() != mapY) return false;
		if (!service_.npcCourse(instanceId, returned) || returned.position != frozen.position || returned.routeIndex != frozen.routeIndex)
			return false;

		const std::string label = "movement-test";
		const std::string route = "\"route\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0}]";
		auto rejects = [&](const std::string& json, const std::string& field) {
			try
			{
				characters_.checkMovement(json, label);
				return false;
			}
			catch (const std::exception& error)
			{
				const std::string text = error.what();
				return text.find(label) != std::string::npos && text.find(field) != std::string::npos;
			}
		};
		if (!rejects("{" + route + ",\"loop\":true}", "move.speed")) return false;
		if (!rejects("{\"speed\":0," + route + ",\"loop\":true}", "move.speed")) return false;
		if (!rejects("{\"speed\":-1," + route + ",\"loop\":true}", "move.speed")) return false;
		if (!rejects("{\"speed\":\"0.8\"," + route + ",\"loop\":true}", "move.speed")) return false;
		if (!rejects("{\"speed\":1,\"type\":\"route\"," + route + ",\"loop\":true}", "unknown key type")) return false;
		if (!rejects("{\"speed\":1,\"intervalMs\":100," + route + ",\"loop\":true}", "unknown key intervalMs")) return false;
		if (!rejects("{\"speed\":1,\"idleChance\":0.5," + route + ",\"loop\":true}", "unknown key idleChance")) return false;
		if (!rejects("{\"speed\":1," + route + ",\"loop\":true,\"extra\":1}", "unknown key extra")) return false;
		if (!rejects("{\"speed\":1," + route + ",\"radius\":1,\"loop\":true}", "both set")) return false;
		if (!rejects("{\"speed\":1}", "route or radius")) return false;
		if (!rejects("{\"speed\":1,\"route\":[{\"x\":1,\"y\":0},{\"x\":2,\"y\":0}],\"loop\":false}", "must be the origin")) return false;
		if (!rejects("{\"speed\":1,\"route\":[{\"x\":0,\"y\":0},{\"x\":2,\"y\":0}],\"loop\":false}", "not adjacent")) return false;
		if (!rejects("{\"speed\":1,\"route\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0},{\"x\":2,\"y\":0}],\"loop\":true}", "not a loop")) return false;
		if (!rejects("{\"speed\":1,\"radius\":-1}", "move.radius")) return false;
		try
		{
			characters_.checkMovement("{\"speed\":1,\"route\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":0},{\"x\":2,\"y\":0}],\"loop\":false}", label);
			characters_.checkMovement("{\"speed\":1,\"radius\":0}", label);
		}
		catch (const std::exception&)
		{
			return false;
		}
		return service_.playerCell() == home;
	}

	bool Game::selfTest(const GameSettings& settings)
	{
		openChapter(settings);
		const Cell player = service_.playerCell();
		if (maps_.projectName() != settings.project) return false;
		if (!maps_.walkable(player)) return false;

		maps_.follow(player);
		if (static_cast<int>(maps_.view().tiles.size()) != maps_.viewRows()
			|| static_cast<int>(maps_.view().tiles.front().size()) != maps_.viewColumns())
			return false;
		const Cell shift = settings.viewShift;
		const auto origin = maps_.view().origin;
		maps_.follow(player + shift);
		if (maps_.view().origin != origin + shift) return false;
		const auto center = maps_.view().tiles[static_cast<std::size_t>(maps_.viewRows() / 2)][static_cast<std::size_t>(maps_.viewColumns() / 2)];
		if (center != maps_.tileAt(player.x + shift.x, player.y + shift.y)) return false;
		maps_.follow(player);

		if (!characters_.contains(settings.characterId)) return false;
		const auto& definition = characters_.character(settings.characterId);
		if (definition.id != settings.characterId || definition.name != characters_.name(settings.characterId)) return false;
		if (characters_.actor(service_.player()).body.id != settings.characterId) return false;
		const auto& animation = definition.animation(definition.defaultState, definition.defaultDirection);
		const auto size = spriteTexture(animation).getSize();
		const auto& sheet = characters_.config();
		if (size.x != static_cast<unsigned>(sheet.frameWidth * animation.frames) || size.y != static_cast<unsigned>(sheet.frameHeight))
			return false;
		if (!checkPlacements(settings) || !checkNpcMovement()) return false;

		Animation sample;
		sample.frames = 4;
		sample.fps = 8.f;
		sample.loop = true;
		if (animationFrame(sample, 0.f) != 0 || animationFrame(sample, 0.125f) != 1 || animationFrame(sample, 0.5f) != 0)
			return false;
		sample.loop = false;
		if (animationFrame(sample, 0.5f) != 3) return false;
		sample.fps = 0.f;
		if (animationFrame(sample, 0.5f) != 0) return false;
		sample.fps = 8.f;
		sample.frames = 1;
		if (animationFrame(sample, 0.5f) != 0) return false;

		const int actorId = service_.player();
		const Cell start = service_.playerCell();
		const Cell steps[] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
		const char* names[] = { "up", "down", "left", "right" };
		for (int i = 0; i < 4; ++i)
		{
			const auto& walk = definition.animation("walk", names[i]);
			const auto& idle = definition.animation("idle", names[i]);
			const float frameTime = 1.f / walk.fps;
			if (animationFrame(walk, frameTime) != 1 || animationFrame(walk, frameTime * static_cast<float>(walk.frames)) != 0)
				return false;
			if (animationFrame(idle, 1.f) != 0) return false;
			characters_.advance(0.4f);
			characters_.at(actorId).walk(names[i]);
			if (characters_.at(actorId).elapsed != 0.f) return false;
			characters_.advance(0.2f);
			characters_.at(actorId).walk(names[i]);
			if (characters_.at(actorId).state != "walk" || characters_.at(actorId).direction != names[i] || characters_.at(actorId).elapsed != 0.2f)
				return false;
			if (!maps_.walkable(start + steps[i])) continue;
			service_.movePlayer(steps[i]);
			if (characters_.at(actorId).state != "walk" || characters_.at(actorId).direction != names[i] || service_.playerCell() != start + steps[i])
				return false;
			service_.stopPlayer();
			if (characters_.at(actorId).state != "idle" || characters_.at(actorId).direction != names[i]) return false;
			service_.movePlayer({ -steps[i].x, -steps[i].y });
			service_.stopPlayer();
			if (service_.playerCell() != start) return false;
		}

		bool blocked = false;
		for (int y = 0; y < maps_.current().height && !blocked; ++y)
			for (int x = 0; x < maps_.current().width && !blocked; ++x)
			{
				if (!maps_.walkable({ x, y })) continue;
				for (int i = 0; i < 4; ++i)
				{
					const Cell next{ x + steps[i].x, y + steps[i].y };
					const bool inside = next.x >= 0 && next.y >= 0 && next.x < maps_.current().width && next.y < maps_.current().height;
					if (!inside || maps_.walkable(next)) continue;
					characters_.move(actorId, x, y);
					service_.movePlayer(steps[i]);
					const auto& blockedActor = characters_.actor(actorId);
					if (blockedActor.body.state != "idle" || blockedActor.body.direction != names[i] || blockedActor.x != x || blockedActor.y != y) return false;
					blocked = true;
					break;
				}
			}
		if (!blocked) return false;
		characters_.move(actorId, start.x, start.y);
		characters_.at(actorId).idle(definition.defaultDirection);

		return maps_.currentChapter() == settings.chapter
			&& maps_.currentMapX() == settings.startMap.x
			&& maps_.currentMapY() == settings.startMap.y;
	}

	int Game::play(const GameSettings& settings)
	{
		openChapter(settings);
		maps_.follow(camera_.focus(maps_, service_.playerCell()));
		const auto& config = maps_.config();
		const auto width = static_cast<unsigned>(maps_.viewColumns() * config.tileSize + config.margin.x * 2);
		const auto height = static_cast<unsigned>(maps_.viewRows() * config.tileSize + config.margin.y * 2);
		const std::string title = config.windowTitle + " - " + maps_.projectName();
		Log::info("window " + std::to_string(width) + "x" + std::to_string(height) + " " + title);
		sf::RenderWindow window(sf::VideoMode({ width, height }), sf::String::fromUtf8(title.begin(), title.end()));
		window.setFramerateLimit(static_cast<unsigned>(config.framerate));
		constexpr sf::Keyboard::Key directions[] = {
			sf::Keyboard::Key::Up, sf::Keyboard::Key::Down, sf::Keyboard::Key::Left, sf::Keyboard::Key::Right };
		std::array<bool, 4> held{};
		int moving = -1;
		sf::Clock repeat;
		sf::Clock animation;
		const sf::Time repeatInterval = sf::milliseconds(settings.moveInterval);
		while (window.isOpen())
		{
			while (const auto event = window.pollEvent())
			{
				if (event->is<sf::Event::Closed>())
				{
					Log::info("window closed");
					window.close();
				}
				else if (event->is<sf::Event::FocusLost>())
				{
					Log::info("focus lost");
					held.fill(false);
					moving = -1;
					service_.stopPlayer();
				}
				else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
				{
					for (int i = 0; i < 4; ++i)
					{
						if (key->code != directions[i] || held[static_cast<std::size_t>(i)]) continue;
						held[static_cast<std::size_t>(i)] = true;
						moving = i;
						moveCharacter(directionStep(key->code));
						repeat.restart();
					}
				}
				else if (const auto* key = event->getIf<sf::Event::KeyReleased>())
				{
					for (int i = 0; i < 4; ++i)
						if (key->code == directions[i]) held[static_cast<std::size_t>(i)] = false;
					if (moving >= 0 && !held[static_cast<std::size_t>(moving)])
					{
						moving = -1;
						for (int i = 0; i < 4; ++i)
							if (held[static_cast<std::size_t>(i)]) moving = i;
						if (moving < 0) service_.stopPlayer();
						repeat.restart();
					}
				}
			}
			if (auto arrived = maps_.pollNeighbor())
			{
				Log::info("neighbor landing " + std::to_string(arrived->x) + "," + std::to_string(arrived->y));
				service_.arrive(*arrived);
				maps_.follow(camera_.focus(maps_, service_.playerCell()));
				repeat.restart();
			}
			window.clear(toSf(config.background));
			if (maps_.loading())
			{
				animation.restart();
				drawLoading(window);
			}
			else
			{
				if (moving >= 0 && repeat.getElapsedTime() >= repeatInterval)
				{
					moveCharacter(directionStep(directions[moving]));
					repeat.restart();
				}
				const float deltaSeconds = animation.restart().asSeconds();
				service_.advanceNpcs(deltaSeconds);
				characters_.advance(deltaSeconds);
				drawGame(window);
			}
			window.display();
		}
		return 0;
	}

	int Game::run(const GameSettings& settings, bool selfTest)
	{
		try
		{
			if (selfTest)
			{
				const bool passed = this->selfTest(settings);
				if (passed) Log::info("self-test passed");
				else Log::error("self-test failed");
				return passed ? 0 : 1;
			}
			return play(settings);
		}
		catch (const std::exception& error)
		{
			try { Log::error(std::string("game stopped: ") + error.what()); }
			catch (...) {}
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
