#include "Game.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace maptool
{

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

	void Game::drawMap(sf::RenderTarget& out, const GameSettings& settings)
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
		const auto local = character_->cell() - view.origin;
		const float characterRadius = config.tileSize * settings.characterRadiusScale;
		sf::CircleShape body(characterRadius);
		body.setOrigin({ characterRadius, characterRadius });
		body.setPosition(margin + sf::Vector2f{ (local.x + .5f) * config.tileSize, (local.y + .5f) * config.tileSize });
		body.setFillColor(toSf(settings.characterColor));
		out.draw(body);
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

	bool Game::moveCharacter(Cell step)
	{
		if (step == Cell{} || maps_.loading()) return false;
		const auto next = character_->cell() + step;
		if (maps_.walkable(next))
			character_->moveTo(next);
		else if (maps_.beginNeighbor(next))
			return true;
		else
			return false;
		maps_.follow(camera_.focus(maps_, character_->cell()));
		return true;
	}

	void Game::openChapter(int chapter)
	{
		maps_.loadChapter(chapter);
		character_ = std::make_unique<TestCharacter>(maps_);
	}

	bool Game::selfTest(const GameSettings& settings)
	{
		openChapter(settings.chapter);
		if (maps_.projectName() != settings.project) return false;
		if (!maps_.walkable(character_->cell())) return false;

		maps_.follow(character_->cell());
		if (static_cast<int>(maps_.view().tiles.size()) != maps_.viewRows()
			|| static_cast<int>(maps_.view().tiles.front().size()) != maps_.viewColumns())
			return false;
		const Cell shift = settings.viewShift;
		const auto origin = maps_.view().origin;
		maps_.follow(character_->cell() + shift);
		if (maps_.view().origin != origin + shift) return false;
		const auto center = maps_.view().tiles[static_cast<std::size_t>(maps_.viewRows() / 2)][static_cast<std::size_t>(maps_.viewColumns() / 2)];
		if (center != maps_.tileAt(character_->cell().x + shift.x, character_->cell().y + shift.y)) return false;
		maps_.follow(character_->cell());

		return maps_.currentChapter() == settings.chapter
			&& maps_.currentMapX() == settings.startMap.x
			&& maps_.currentMapY() == settings.startMap.y;
	}

	int Game::play(const GameSettings& settings)
	{
		openChapter(settings.chapter);
		maps_.follow(camera_.focus(maps_, character_->cell()));
		const auto& config = maps_.config();
		const auto width = static_cast<unsigned>(maps_.viewColumns() * config.tileSize + config.margin.x * 2);
		const auto height = static_cast<unsigned>(maps_.viewRows() * config.tileSize + config.margin.y * 2);
		const std::string title = config.windowTitle + " - " + maps_.projectName();
		sf::RenderWindow window(sf::VideoMode({ width, height }), sf::String::fromUtf8(title.begin(), title.end()));
		window.setFramerateLimit(static_cast<unsigned>(config.framerate));
		constexpr sf::Keyboard::Key directions[] = {
			sf::Keyboard::Key::Up, sf::Keyboard::Key::Down, sf::Keyboard::Key::Left, sf::Keyboard::Key::Right };
		std::array<bool, 4> held{};
		int moving = -1;
		sf::Clock repeat;
		const sf::Time repeatInterval = sf::milliseconds(110);
		while (window.isOpen())
		{
			while (const auto event = window.pollEvent())
			{
				if (event->is<sf::Event::Closed>()) window.close();
				else if (event->is<sf::Event::FocusLost>())
				{
					held.fill(false);
					moving = -1;
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
						repeat.restart();
					}
				}
			}
			if (auto arrived = maps_.pollNeighbor())
			{
				character_->moveTo(*arrived);
				maps_.follow(camera_.focus(maps_, character_->cell()));
				repeat.restart();
			}
			window.clear(toSf(config.background));
			if (maps_.loading())
				drawLoading(window);
			else
			{
				if (moving >= 0 && repeat.getElapsedTime() >= repeatInterval)
				{
					moveCharacter(directionStep(directions[moving]));
					repeat.restart();
				}
				drawMap(window, settings);
			}
			window.display();
		}
		return 0;
	}

	int Game::run(const GameSettings& settings, bool selfTest)
	{
		try
		{
			if (selfTest) return this->selfTest(settings) ? 0 : 1;
			return play(settings);
		}
		catch (const std::exception& error)
		{
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
