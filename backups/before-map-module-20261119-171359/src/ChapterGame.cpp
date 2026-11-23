#include "ChapterGame.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>

namespace chapter
{
namespace
{
int distance(sf::Vector2i a, sf::Vector2i b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
constexpr std::array<sf::Vector2i, 4> Directions{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
sf::Vector2f worldCenter(sf::Vector2i p) { return MapOrigin + sf::Vector2f{(p.x + .5f) * MapTile, (p.y + .5f) * MapTile}; }
sf::Vector2f battleCenter(sf::Vector2i p) { return BattleOrigin + sf::Vector2f{(p.x + .5f) * BattleTile, (p.y + .5f) * BattleTile}; }
std::optional<sf::Vector2i> cellAt(sf::Vector2f p, sf::Vector2f origin, float tile, int w, int h)
{
    p -= origin;
    if (p.x < 0 || p.y < 0 || p.x >= w * tile || p.y >= h * tile) return std::nullopt;
    return sf::Vector2i{static_cast<int>(p.x / tile), static_cast<int>(p.y / tile)};
}
template<class Passable>
std::vector<sf::Vector2i> path(sf::Vector2i from, sf::Vector2i to, int w, int h, Passable pass)
{
    auto inside = [=](sf::Vector2i p) { return p.x >= 0 && p.y >= 0 && p.x < w && p.y < h; };
    if (!inside(from) || !inside(to) || !pass(to) || from == to) return {};
    auto idx = [w](sf::Vector2i p) { return p.y * w + p.x; };
    std::vector<int> parent(w * h, -1);
    parent[idx(from)] = idx(from);
    std::queue<sf::Vector2i> q;
    q.push(from);
    while (!q.empty() && parent[idx(to)] == -1)
    {
        const auto p = q.front(); q.pop();
        for (auto d : Directions)
        {
            auto n = p + d;
            if (!inside(n) || !pass(n) || parent[idx(n)] != -1) continue;
            parent[idx(n)] = idx(p); q.push(n);
        }
    }
    if (parent[idx(to)] == -1) return {};
    std::vector<sf::Vector2i> result;
    for (int p = idx(to); p != idx(from); p = parent[p]) result.push_back({p % w, p / w});
    std::reverse(result.begin(), result.end());
    return result;
}
void box(sf::RenderTarget& out, sf::Vector2f p, sf::Vector2f size, sf::Color color)
{
    sf::RectangleShape shape(size); shape.setPosition(p); shape.setFillColor(color); out.draw(shape);
}
void oval(sf::RenderTarget& out, sf::Vector2f p, sf::Vector2f radius, sf::Color color)
{
    sf::CircleShape shape(1.f, 24); shape.setOrigin({1, 1}); shape.setScale(radius);
    shape.setPosition(p); shape.setFillColor(color); out.draw(shape);
}
sf::String utf8(const std::string& s) { return sf::String::fromUtf8(s.begin(), s.end()); }
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

bool saveProgress(const std::filesystem::path& file, const Progress& p)
{
    try
    {
        std::filesystem::create_directories(file.parent_path());
        auto temporary = file; temporary += ".tmp";
        std::ofstream out(temporary);
        out << "MONSTER_TACTICS_CH1 1\n" << p.stage << ' ' << p.battleMask << ' ' << p.herbs << ' '
            << p.map << ' ' << p.flags << ' ' << p.cell.x << ' ' << p.cell.y << '\n';
        for (bool r : p.recruited) out << r << ' ';
        out << '\n' << p.party.size();
        for (int i : p.party) out << ' ' << i;
        out << '\n'; out.flush();
        if (!out) return false;
        out.close();
        if (std::filesystem::exists(file))
            std::filesystem::copy_file(file, file.string() + ".bak", std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(temporary, file, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(temporary);
        return true;
    }
    catch (...) { return false; }
}
std::optional<Progress> loadProgress(const std::filesystem::path& file)
{
    std::ifstream in(file);
    std::string magic; int version = 0;
    Progress p;
    if (!(in >> magic >> version) || magic != "MONSTER_TACTICS_CH1" || version != 1) return {};
    if (!(in >> p.stage >> p.battleMask >> p.herbs >> p.map >> p.flags >> p.cell.x >> p.cell.y)) return {};
    if (p.stage < 0 || p.stage > 5 || p.battleMask < 0 || p.battleMask > 7 || p.herbs < 0 || p.herbs > 6 ||
        p.map < 0 || p.map > 2 || p.flags > 511 || p.cell.x < 0 || p.cell.x >= MapW || p.cell.y < 0 || p.cell.y >= MapH) return {};
    for (auto& r : p.recruited) { int value; if (!(in >> value) || (value != 0 && value != 1)) return {}; r = value != 0; }
    int count = 0; if (!(in >> count) || count < 0 || count > 4) return {};
    std::set<int> seen;
    for (int n = 0; n < count; ++n)
    {
        int id; if (!(in >> id) || id < 0 || id >= 10 || !p.recruited[id] || !seen.insert(id).second) return {};
        p.party.push_back(id);
    }
    // Story flags are monotonic and must agree with completed mandatory encounters.
    if ((p.stage >= 1 && !(p.battleMask & 1)) || (p.stage >= 3 && !(p.battleMask & 2)) ||
        (p.stage >= 4 && !(p.battleMask & 4))) return {};
    return p;
}

Game::Game(std::filesystem::path file) : savePath(std::move(file)) { buildRegion(); }
void Game::loadFont()
{
    const std::array<std::filesystem::path, 4> paths{
        savePath.parent_path().parent_path() / "assets/NotoSansKR-Regular.ttf",
        std::filesystem::path("C:/Windows/Fonts/malgun.ttf"),
        std::filesystem::path("C:/Windows/Fonts/gulim.ttc"),
        std::filesystem::path("assets/NotoSansKR-Regular.ttf")};
    for (const auto& file : paths)
        if (std::filesystem::exists(file) && font.openFromFile(file)) { fontReady = true; break; }
}
int Game::level() const { return 1 + std::popcount(static_cast<unsigned>(progress.battleMask)); }
void Game::checkpoint()
{
    if (previewMode) return;
    if (!saveProgress(savePath, progress)) notice = "저장 실패: 저장 폴더의 쓰기 권한을 확인해 주세요.";
}
void Game::restore()
{
    auto loaded = loadProgress(savePath);
    if (!loaded) { notice = "불러올 수 있는 저장 파일이 없습니다."; return; }
    progress = *loaded; scene = Scene::Explore; dialog.reset(); walkRoute.clear(); pendingEvent = -1;
    buildRegion();
    if (!worldWalkable(progress.cell)) progress.cell = {2, 7};
    heroPosition = worldCenter(progress.cell); notice = "저장한 여행을 이어갑니다.";
}
void Game::newGame()
{
    if (std::filesystem::exists(savePath))
    {
        std::error_code error;
        std::filesystem::copy_file(savePath, savePath.parent_path() / "chapter1.previous.sav",
            std::filesystem::copy_options::overwrite_existing, error);
        if (error) { notice = "이전 여행 백업에 실패하여 새 여행을 시작하지 않았습니다."; return; }
    }
    progress = {}; scene = Scene::Explore; walkRoute.clear(); walkStep = 0; pendingEvent = -1;
    buildRegion(); heroPosition = worldCenter(progress.cell);
    showDialog("1장 · 숲 밖의 식탁", {
        "루안은 인간인 남자 청년이다. 하지만 그를 키워 준 가족은 고요나무 숲의 몬스터들이었다. 어머니는 오래전 이 숲으로 도망쳐 와 그를 낳은 뒤 세상을 떠났다.",
        "오늘 루안은 어머니가 남긴 목걸이를 들고 처음으로 긴 여행을 시작한다. 숲의 어른은 목걸이의 문양을 본 사람이 돌여울 마을에 있을지 모른다고 말했다.",
        "루안: 어머니가 어떤 사람이었는지 알고 싶어요. 그리고 숲 밖의 세상도 직접 보고 올게요.\n\n사람이나 표식을 클릭하면 그곳으로 걸어가 대화합니다. 동료 계약은 모두 선택이며, 거점에서 최대 4명의 동료를 편성할 수 있습니다."
    }, [this] { checkpoint(); }, "여행 시작");
}
bool Game::worldWalkable(sf::Vector2i p) const
{
    return p.x >= 0 && p.y >= 0 && p.x < MapW && p.y < MapH &&
           (terrain[p.y][p.x] == '.' || terrain[p.y][p.x] == ':');
}
void Game::buildRegion()
{
    for (int y = 0; y < MapH; ++y)
        for (int x = 0; x < MapW; ++x)
        {
            const int seed = (x * 17 + y * 31 + progress.map * 11) % 23;
            char t = '.';
            if (x == 0 || y == 0 || x == MapW - 1 || y == MapH - 1) t = 'T';
            else if (seed < (progress.map == 2 ? 2 : 5)) t = 'T';
            else if (seed == 7) t = 'R';
            if (progress.map == 0 && x >= 9 && x <= 11 && y >= 4 && y <= 6) t = '~';
            if (progress.map == 1 && x == 10 && y != 7 && y >= 3 && y <= 10) t = '~';
            if (progress.map == 2 && ((x >= 4 && x <= 8 && y >= 1 && y <= 2) ||
                (x >= 11 && x <= 14 && y >= 3 && y <= 5) || (x >= 6 && x <= 9 && y >= 10 && y <= 12))) t = 'H';
            terrain[y][x] = t;
        }
    // Keep every optional encounter accessible without requiring a particular companion.
    for (const auto& e : Events)
    {
        if (e.map != progress.map) continue;
        int x = 2, y = 7;
        while (x != e.x) { terrain[y][x] = ':'; x += e.x > x ? 1 : -1; }
        while (y != e.y) { terrain[y][x] = ':'; y += e.y > y ? 1 : -1; }
        terrain[y][x] = ':';
    }
    terrain[7][2] = ':';
    heroPosition = worldCenter(progress.cell);
}
std::vector<sf::Vector2i> Game::worldPath(sf::Vector2i from, sf::Vector2i to) const
{
    return path(from, to, MapW, MapH, [this](auto p) { return worldWalkable(p); });
}
bool Game::eventVisible(const WorldEvent& e) const
{
    if (e.map != progress.map) return false;
    if (e.kind == EventKind::Monster) return !progress.recruited[e.value];
    if (e.kind == EventKind::Herb || e.kind == EventKind::Letter) return !(progress.flags & (1u << e.value));
    return true;
}
void Game::showDialog(std::string title, std::vector<std::string> pages,
                      std::function<void()> action, std::string accept, bool choice)
{
    dialog = Dialog{std::move(title), std::move(pages), 0, std::move(accept), std::move(action), choice};
}
void Game::advanceDialog()
{
    if (!dialog) return;
    if (dialog->page + 1 < dialog->pages.size()) { ++dialog->page; return; }
    auto action = std::move(dialog->action); dialog.reset();
    if (action) action();
}
bool Game::canRecruit(int id) const
{
    switch (Monsters[id].need)
    {
    case Need::Herb: return progress.herbs >= 1;
    case Need::Victory: return (progress.battleMask & 1) != 0;
    case Need::Shrine: return (progress.flags & (1u << 6)) != 0;
    case Need::Kindness: return (progress.flags & (1u << 8)) != 0;
    case Need::Letter: return (progress.flags & (1u << 7)) != 0;
    default: return true;
    }
}
void Game::recruit(int id)
{
    if (progress.recruited[id] || !canRecruit(id)) return;
    if (Monsters[id].need == Need::Herb) --progress.herbs;
    progress.recruited[id] = true;
    if (progress.party.size() < 4) progress.party.push_back(id);
    notice = std::string(Monsters[id].name) + "와 동행 계약을 맺었습니다. 거점에서 편성할 수 있습니다.";
    checkpoint();
}
void Game::toggleParty(int id)
{
    if (!progress.recruited[id]) { notice = Monsters[id].requirement; return; }
    auto it = std::find(progress.party.begin(), progress.party.end(), id);
    if (it != progress.party.end()) progress.party.erase(it);
    else if (progress.party.size() < 4) progress.party.push_back(id);
    else { notice = "주인공을 포함해 최대 5명입니다. 다른 동료를 먼저 대기로 바꿔 주세요."; return; }
    checkpoint();
}
void Game::travel(int region)
{
    if (region == 2 && progress.stage == 0)
    { showDialog("닫힌 길", {"마을로 가는 길을 검문대가 막고 있다. 먼저 바람길 중앙의 검문소를 조사하자."}); return; }
    const int previous = progress.map;
    progress.map = region;
    progress.cell = (region < previous) ? sf::Vector2i{19, 7} : sf::Vector2i{2, 7};
    if (region == 0) progress.cell = {19, 7};
    walkRoute.clear(); pendingEvent = -1; buildRegion(); checkpoint();
    notice = std::string(RegionNames[region]) + "에 도착했습니다.";
}
void Game::interact(int eventIndex)
{
    const auto e = Events.at(eventIndex);
    if (!eventVisible(e)) return;
    switch (e.kind)
    {
    case EventKind::Monster:
    {
        const auto& m = Monsters[e.value];
        if (canRecruit(e.value))
            showDialog(std::string(m.name) + " · " + m.species, {m.introduction, m.motive},
                [this, id = e.value] { recruit(id); }, "서로 동의하고 계약", true);
        else showDialog(std::string(m.name) + " · " + m.species,
            {std::string(m.introduction) + "\n\n영입 조건: " + m.requirement});
        break;
    }
    case EventKind::Herb:
        progress.flags |= 1u << e.value; ++progress.herbs; checkpoint();
        notice = "푸른 약초를 얻었습니다. 동료의 부탁에 사용할 수 있습니다."; break;
    case EventKind::Shrine:
        progress.flags |= 1u << e.value; checkpoint();
        showDialog("돌에 남은 약속", {"오래된 문양에는 인간과 골렘이 함께 다리를 세우는 모습이 새겨져 있다.\n\n루안: 함께 살았다는 건 옛이야기만은 아니었구나. 도른에게 이 모습을 전해 주자."}); break;
    case EventKind::Letter:
        progress.flags |= 1u << e.value; checkpoint();
        showDialog("받는 이: 누아", {"길가에서 접힌 편지를 발견했다. 봉투에 누아라는 이름이 적혀 있다.\n\n남의 편지를 펼치지 않고 조심히 챙겼다. 돌여울 마을에서 주인을 찾아보자."}); break;
    case EventKind::Elder:
        showDialog("숲의 어른", {"숲의 어른: 루안, 네 어머니의 이름은 세린이란다. 우리가 아는 건 이 목걸이와, 널 지키려 애썼던 모습뿐이야.",
            "숲의 어른: 돌여울 마을의 기록원은 오래된 여행 기록을 보관하고 있어. 사람들을 만나 보렴. 다만 그들이 모두 너와 같은 상식을 갖고 있지는 않을 거야.",
            "루안: 돌아올 곳이 있다는 건 알아요. 다녀올게요.\n\n동료는 물건처럼 소유하는 존재가 아닙니다. 부탁을 해결한 뒤에도 계약 여부는 자유롭게 선택할 수 있습니다."}); break;
    case EventKind::Exit: travel(e.value); break;
    case EventKind::Cook:
        progress.flags |= 1u << e.value; checkpoint();
        showDialog("요리사 마레", {"마레: 숲에서 왔다고? 배고프면 앉아. 내 식탁에서는 뿔이나 날개보다 빈 그릇이 먼저 보이거든.",
            "마레: 제국이 정한 금지 사항을 모두 마음에 품고 사는 건 아니야. 공존을 바라는 인간도 있다는 걸 네 친구들에게 전해 줘.\n\n나무정령 이오와 여우족 유리에게 이 이야기를 전할 수 있습니다."}); break;
    case EventKind::Archivist:
        if (progress.stage == 1)
            showDialog("목걸이의 문양", {"이안: 이 문양은 북부 우편소에서 쓰던 여행 표식이군요. 세린이라는 이름도 장부에서 본 기억이 있습니다.",
                "이안: 하지만 지금 보관실은 봉쇄됐어요. 레오르 감독관이 주민들의 식량과 문서까지 압수하고 있습니다. 마을 동쪽 수레에 주민들이 붙잡혀 있어요.",
                "루안: 제가 돌아오면 기록을 함께 찾아봐 주세요. 우선 저 사람들을 도와야겠어요."},
                [this] { progress.stage = 2; checkpoint(); }, "수레로 향한다");
        else if (progress.stage == 4)
            showDialog("이름이 이어지는 곳", {"이안: 압수 장부와 주민들의 증언이 일치합니다. 이 기록을 인근 마을에도 보내겠습니다. 감독관 한 사람을 몰아낸 것만으로 모든 것이 바뀌지는 않겠지만요.",
                "이안: 그리고 세린의 여행 기록을 찾았어요. 그녀는 북부 우편소를 지나 남쪽 숲으로 향했습니다. 당시 누군가를 피해 이동했던 것 같습니다. 다음 단서는 북부에 있을 거예요.",
                "마레는 인간과 몬스터 모두에게 식사를 내어 주었다. 빈 의자가 조금씩 채워졌다. 루안은 목걸이를 쥐고, 처음으로 숲 밖에도 돌아올 식탁이 생겼다고 생각했다."},
                [this] { progress.stage = 5; checkpoint(); scene = Scene::Ending; }, "1장을 마친다");
        else showDialog("기록원 이안", {progress.stage == 5 ? "이안: 북부 우편소로 가는 길은 다음 여정이 되겠군요. 이곳에서 잠시 쉬며 동료들과 준비하세요." :
                progress.stage == 3 ? "이안: 수레에서 찾은 운송표에 바람길 북동쪽 보급 창고가 적혀 있어요. 그곳의 원본 장부가 있어야 압수를 명령한 자의 책임을 밝힐 수 있습니다." :
                progress.stage == 2 ? "이안: 마을 동쪽의 압수 수레를 먼저 살펴봐 주세요. 주민들이 걱정됩니다." :
                "이안: 여행 기록을 찾으시나요? 길목이 소란스럽군요. 안전하게 오셨으면 좋겠습니다."}); break;
    case EventKind::Battle:
        if (progress.battleMask & (1 << e.value))
        {
            showDialog("해결한 사건", {"이곳의 사건은 해결되었습니다. 다른 편성으로 전투를 다시 시험할 수 있습니다. 다시 승리해도 레벨과 이야기 보상은 중복 지급되지 않습니다."},
                [this, id = e.value] { startBattle(id); }, "훈련 전투", true); break;
        }
        if ((e.value == 1 && progress.stage < 2) || (e.value == 2 && progress.stage < 3))
        { showDialog("아직 닫힌 문", {e.value == 1 ? "먼저 기록원 이안에게 어머니의 흔적을 물어보자." : "창고의 용도를 알 수 없다. 먼저 돌여울 마을의 사건을 조사하자."}); break; }
        if (e.value == 0)
            showDialog("길을 막은 검문", {"상인: 통행증도 보여 줬습니다. 왜 숲의 주민이라는 이유로 짐을 빼앗는 겁니까?\n\n검문병: 감독관의 명령이다. 몬스터와 거래한 물자는 조사 대상이야.",
                "루안: 그 물건은 당신들 것이 아니잖아요. 길을 열어 주세요.\n\n승리 목표: 적 전원 전투 불능. 주인공을 포함한 아군이 모두 쓰러지면 패배합니다. 전투 불능에 영구 사망은 없습니다."},
                [this] { startBattle(0); }, "현재 편성으로 전투", true);
        else if (e.value == 1)
            showDialog("돌려받아야 할 식량", {"주민: 이 곡식은 겨울을 날 식량이에요. 세금은 이미 냈어요!\n\n징수병: 장부에 없는 몬스터에게 나눠 준 양까지 다시 거둬야 한다.",
                "루안: 누가 먹었는지가 곡식의 주인을 바꾸지는 않아요. 내려놓으세요.\n\n수레를 지키는 징수대를 제압하고 주민들을 돕자."}, [this] { startBattle(1); }, "현재 편성으로 전투", true);
        else showDialog("닫힌 창고의 장부", {"레오르: 제국의 땅에서 제국의 명령을 따르는 게 뭐가 잘못이지?\n\n루안: 명령이라는 말로 남의 삶을 가져갈 수는 없어요.",
                "주민을 돕는 인간 경비병이 창고 뒤편으로 물러난다.\n\n경비병: 장부는 저 안에 있습니다. 이 일에 더는 가담하지 않겠습니다.\n\n감독관과 호위대를 제압하고 압수 명령의 원본을 확보하자."},
                [this] { startBattle(2); }, "현재 편성으로 전투", true);
        break;
    }
}

Actor Game::makeAlly(int type, sf::Vector2i position) const
{
    Actor a; a.type = type; a.cell = position;
    const int growth = level() - 1;
    if (type == -1)
    {
        a.name = "루안"; a.maxHp = 52 + growth * 5; a.attack = 12 + growth * 2;
        a.defense = 3; a.movement = 4; a.range = 2; a.maxMp = 15;
        a.color = {101, 182, 174};
    }
    else
    {
        const auto& m = Monsters[type]; a.name = m.name;
        a.maxHp = m.hp + growth * 4; a.attack = m.attack + growth * 2; a.defense = m.defense;
        a.movement = m.move; a.range = m.range; a.maxMp = m.mp; a.color = m.color;
    }
    a.hp = a.maxHp; a.mp = a.maxMp;
    return a;
}
void Game::startBattle(int id)
{
    checkpoint();
    battleId = id; scene = Scene::Battle; dialog.reset(); selected = 0; command = Command::Move;
    enemyTurn = false; round = 1; potions = 3; log.clear(); walkRoute.clear(); pendingEvent = -1;
    for (int y = 0; y < BattleH; ++y)
        for (int x = 0; x < BattleW; ++x) field[y][x] = '.';
    for (auto p : std::array<sf::Vector2i, 6>{{{4, 1}, {4, 2}, {6, 4}, {6, 5}, {4, 7}, {8, 7}}})
        field[p.y][p.x] = '#';
    actors.clear(); actors.push_back(makeAlly(-1, {1, 4}));
    constexpr std::array<sf::Vector2i, 4> spawn{{{1, 2}, {1, 6}, {2, 3}, {2, 5}}};
    for (std::size_t i = 0; i < progress.party.size(); ++i)
        actors.push_back(makeAlly(progress.party[i], spawn[i]));
    const int allies = static_cast<int>(actors.size());
    const int count = std::min(7, allies + 1 + (id == 2 && allies >= 3 ? 1 : 0));
    constexpr std::array<sf::Vector2i, 7> enemySpawn{{{9, 4}, {10, 2}, {10, 6}, {8, 1}, {8, 6}, {11, 3}, {11, 7}}};
    for (int i = 0; i < count; ++i)
    {
        Actor a; a.enemy = true; a.type = -2; a.cell = enemySpawn[i];
        const bool boss = id == 2 && i == 0;
        a.name = boss ? "레오르 감독관" : (i % 3 == 1 ? "제국 사수" : "제국 경비병");
        a.maxHp = boss ? 32 + allies * 3 : 17 + id * 3 + allies;
        a.hp = a.maxHp; a.attack = 6 + id + (boss ? 2 : 0); a.defense = boss ? 3 : 1;
        a.range = i % 3 == 1 && !boss ? 3 : 1; a.movement = 3;
        a.color = boss ? sf::Color{192, 109, 94} : sf::Color{174, 137, 119};
        actors.push_back(a);
    }
    record("전투 시작. 이동 후 공격하거나, 행동 후 이동할 수 있습니다.");
    record("숫자 1 이동 / 2 공격 / 3 기술 / 4 방어 / 5 물약 / Space 턴 종료");
}
bool Game::openCell(sf::Vector2i p, int ignore) const
{
    if (p.x < 0 || p.y < 0 || p.x >= BattleW || p.y >= BattleH || field[p.y][p.x] == '#') return false;
    for (int i = 0; i < static_cast<int>(actors.size()); ++i)
        if (i != ignore && actors[i].alive() && actors[i].cell == p) return false;
    return true;
}
std::vector<sf::Vector2i> Game::battlePath(sf::Vector2i from, sf::Vector2i to, int who) const
{
    return path(from, to, BattleW, BattleH, [this, who](auto p) { return openCell(p, who); });
}
bool Game::sight(sf::Vector2i from, sf::Vector2i to) const
{
    // Bresenham visibility: scenery blocks ranged attacks; units do not.
    int x = from.x, y = from.y;
    const int dx = std::abs(to.x - x), dy = -std::abs(to.y - y);
    const int sx = x < to.x ? 1 : -1, sy = y < to.y ? 1 : -1;
    int error = dx + dy;
    while (x != to.x || y != to.y)
    {
        const int e = error * 2;
        if (e >= dy) { error += dy; x += sx; }
        if (e <= dx) { error += dx; y += sy; }
        if (x == to.x && y == to.y) return true;
        if (field[y][x] == '#') return false;
    }
    return true;
}
bool Game::inRange(const Actor& a, sf::Vector2i target, int range) const
{
    return distance(a.cell, target) <= range && sight(a.cell, target);
}
int Game::actorAt(sf::Vector2i p) const
{
    for (int i = 0; i < static_cast<int>(actors.size()); ++i)
        if (actors[i].alive() && actors[i].cell == p) return i;
    return -1;
}
void Game::record(const std::string& s)
{
    log.push_back(s); if (log.size() > 30) log.erase(log.begin());
}
void Game::moveActor(int who, sf::Vector2i target)
{
    auto& a = actors[who]; if (!a.alive() || a.moved || a.rooted > 0 || target == a.cell) return;
    auto route = battlePath(a.cell, target, who);
    if (route.empty() || static_cast<int>(route.size()) > a.movement) return;
    a.cell = target; a.moved = true;
}
void Game::damage(int target, int amount)
{
    auto& a = actors[target]; amount = std::max(1, amount);
    const int absorbed = std::min(a.shield, amount); a.shield -= absorbed; amount -= absorbed;
    a.hp = std::max(0, a.hp - amount);
    record(a.name + " 피해 " + std::to_string(amount) + (absorbed ? " (보호막 흡수 " + std::to_string(absorbed) + ")" : ""));
    if (!a.alive()) record(a.name + " 전투 불능");
}
void Game::heal(int target, int amount)
{
    auto& a = actors[target]; const int old = a.hp; a.hp = std::min(a.maxHp, a.hp + amount);
    record(a.name + " 체력 " + std::to_string(a.hp - old) + " 회복");
}
bool Game::attack(int who, int target)
{
    auto& a = actors[who]; auto& b = actors[target];
    if (!a.alive() || a.acted || !b.alive() || a.enemy == b.enemy || !inRange(a, b.cell, a.range)) return false;
    a.acted = true;
    record(a.name + " → " + b.name + " 기본 공격");
    damage(target, a.attack + (a.boosted ? 3 : 0) - std::max(0, b.defense - (b.weakened ? 2 : 0)));
    checkBattleEnd(); return true;
}
bool Game::useSkill(int who, sf::Vector2i target)
{
    auto& a = actors[who];
    const auto skill = a.type == -1 ? Skill::Mend : Monsters[a.type].skill;
    const int cost = a.type == -1 ? 3 : Monsters[a.type].cost;
    const int range = skill == Skill::Fang ? 1 : (skill == Skill::Pollen || skill == Skill::Root || skill == Skill::Venom ? 4 : 3);
    if (!a.alive() || a.acted || a.mp < cost) return false;
    const int targetId = actorAt(target);
    const bool areaFriendly = skill == Skill::Ward || skill == Skill::Rally;
    const bool healing = skill == Skill::Mend || skill == Skill::Pollen;
    if (areaFriendly && target != a.cell) return false;
    if (!areaFriendly)
    {
        if (!inRange(a, target, range) || targetId < 0) return false;
        if (healing ? actors[targetId].enemy != a.enemy : actors[targetId].enemy == a.enemy) return false;
    }
    a.mp -= cost; a.acted = true;
    record(a.name + " · " + (a.type == -1 ? "길잡이의 빛" : Monsters[a.type].technique));
    if (healing) heal(targetId, skill == Skill::Mend ? 16 : 18 + level());
    else if (areaFriendly)
    {
        for (auto& ally : actors)
            if (ally.alive() && ally.enemy == a.enemy && distance(a.cell, ally.cell) <= 2)
            { if (skill == Skill::Ward) ally.shield = std::max(ally.shield, 10); else ally.boosted = 2; }
    }
    else
    {
        const int power = a.attack + (a.boosted ? 3 : 0);
        if (skill == Skill::Flame)
        {
            for (int i = 0; i < static_cast<int>(actors.size()); ++i)
                if (actors[i].alive() && actors[i].enemy != a.enemy && distance(actors[i].cell, target) <= 1)
                    damage(i, power - actors[i].defense + 2);
        }
        else
        {
            auto& b = actors[targetId];
            damage(targetId, power - std::max(0, b.defense - (b.weakened ? 2 : 0)) + (skill == Skill::Fang ? 8 : 2));
            if (skill == Skill::Acid) b.weakened = 2;
            if (skill == Skill::Root) b.rooted = 1;
            if (skill == Skill::Venom) b.poison = 3;
            if (skill == Skill::Drain) heal(who, 9);
            if (skill == Skill::Gust && b.alive())
            {
                auto d = b.cell - a.cell;
                d = std::abs(d.x) >= std::abs(d.y) ? sf::Vector2i{d.x > 0 ? 1 : -1, 0} : sf::Vector2i{0, d.y > 0 ? 1 : -1};
                if (openCell(b.cell + d, targetId)) { b.cell += d; record("돌풍이 대상을 한 칸 밀어냈습니다."); }
            }
        }
    }
    checkBattleEnd(); return true;
}
bool Game::drinkPotion(int who)
{
    auto& a = actors[who]; if (!a.alive() || a.acted || potions <= 0 || a.hp == a.maxHp) return false;
    --potions; a.acted = true; heal(who, 24); return true;
}
void Game::guard(int who)
{
    auto& a = actors[who]; if (!a.alive() || a.acted) return;
    a.acted = true; a.shield = std::max(a.shield, 7); record(a.name + " 방어: 보호막 7");
}
void Game::endPlayerTurn()
{
    if (enemyTurn || scene != Scene::Battle) return;
    enemyTurn = true; enemyCursor = 0; enemyTimer = .15f;
    for (auto& a : actors) if (a.enemy && a.alive()) { a.moved = false; a.acted = false; }
    record("적 진영의 차례");
}
void Game::enemyAction(int who)
{
    auto& a = actors[who]; if (!a.alive()) return;
    if (a.poison > 0) { --a.poison; damage(who, 4); if (!a.alive()) { checkBattleEnd(); return; } }
    // Pick a reachable firing position. A blocked shortest line does not stall the AI.
    int target = -1, best = std::numeric_limits<int>::max();
    sf::Vector2i bestCell = a.cell;
    for (int i = 0; i < static_cast<int>(actors.size()); ++i)
    {
        const auto& b = actors[i]; if (b.enemy || !b.alive()) continue;
        for (int y = 0; y < BattleH; ++y)
            for (int x = 0; x < BattleW; ++x)
            {
                sf::Vector2i p{x, y}; if (!openCell(p, who) || distance(p, b.cell) > a.range || !sight(p, b.cell)) continue;
                const auto route = battlePath(a.cell, p, who);
                if (p != a.cell && route.empty()) continue;
                const int cost = static_cast<int>(route.size()) * 100 + b.hp;
                if (cost < best) { best = cost; target = i; bestCell = p; }
            }
    }
    if (target >= 0)
    {
        if (a.rooted == 0)
        {
            const auto route = battlePath(a.cell, bestCell, who);
            const int steps = std::min(a.movement, static_cast<int>(route.size()));
            if (steps > 0) a.cell = route[steps - 1];
        }
        attack(who, target);
    }
    if (a.rooted > 0) --a.rooted;
    if (a.weakened > 0) --a.weakened;
}
void Game::beginPlayerTurn()
{
    enemyTurn = false; ++round;
    for (auto& a : actors)
        if (!a.enemy && a.alive())
        {
            a.moved = false; a.acted = false; a.shield = 0;
            if (a.boosted > 0) --a.boosted;
        }
    selected = 0;
    while (selected < static_cast<int>(actors.size()) && (actors[selected].enemy || !actors[selected].alive())) ++selected;
    command = Command::Move; record("아군 진영의 차례 · " + std::to_string(round) + "턴");
}
void Game::checkBattleEnd()
{
    if (scene != Scene::Battle) return;
    bool ally = false, enemy = false;
    for (const auto& a : actors) if (a.alive()) { if (a.enemy) enemy = true; else ally = true; }
    if (ally && enemy) return;
    victory = ally; scene = Scene::Result; enemyTurn = false;
}
void Game::finishBattle()
{
    if (!victory) { startBattle(battleId); return; }
    const bool first = !(progress.battleMask & (1 << battleId));
    progress.battleMask |= 1 << battleId;
    if (battleId == 0) progress.stage = std::max(progress.stage, 1);
    if (battleId == 1) progress.stage = std::max(progress.stage, 3);
    if (battleId == 2) progress.stage = std::max(progress.stage, 4);
    scene = Scene::Explore; buildRegion(); checkpoint();
    if (!first) { notice = "훈련을 마쳤습니다. 모든 동료가 회복되었습니다."; return; }
    if (battleId == 0)
        showDialog("열린 길", {"상인: 고맙습니다. 마을의 기록원 이안이라면 찾는 분의 흔적을 알지도 모르겠군요.\n\n길을 다시 열었습니다. 돌여울 마을로 향할 수 있습니다. 모든 동료의 레벨이 함께 오릅니다."});
    if (battleId == 1)
        showDialog("수레 안의 운송표", {"주민들은 식량을 돌려받았다. 수레 안에서는 같은 물자를 여러 번 압수한 흔적과 운송표가 발견되었다.\n\n마레: 이 문서에 적힌 곳은 바람길 북동쪽 창고야. 원본 장부가 있다면 다른 마을의 피해도 막을 수 있을 거야."});
    if (battleId == 2)
        showDialog("남겨진 명령", {"레오르는 물러났다. 일행은 주민들의 이름과 압수량이 적힌 원본 장부를 확보했다.\n\n루안: 이 기록을 마을에 가져가요. 누가 무슨 일을 했는지, 모두가 알 수 있도록.\n\n기록원 이안에게 장부를 전달하자."});
}

void Game::update(float dt)
{
    elapsed += dt;
    if (dialog) return;
    if (scene == Scene::Explore && walkStep < walkRoute.size())
    {
        float budget = 165.f * dt;
        while (walkStep < walkRoute.size())
        {
            const auto target = worldCenter(walkRoute[walkStep]); const auto delta = target - heroPosition;
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (length > budget) { heroPosition += delta * (budget / length); break; }
            heroPosition = target; progress.cell = walkRoute[walkStep++]; budget -= length;
        }
        if (walkStep == walkRoute.size() && pendingEvent >= 0)
        { const int event = pendingEvent; pendingEvent = -1; interact(event); }
    }
    if (scene == Scene::Battle && enemyTurn)
    {
        enemyTimer -= dt;
        if (enemyTimer > 0) return;
        while (enemyCursor < static_cast<int>(actors.size()) && (!actors[enemyCursor].enemy || !actors[enemyCursor].alive())) ++enemyCursor;
        if (enemyCursor >= static_cast<int>(actors.size())) beginPlayerTurn();
        else { enemyAction(enemyCursor++); enemyTimer = .45f; }
    }
}
void Game::click(sf::Vector2f p)
{
    for (const auto& button : buttons)
        if (button.rect.contains(p))
        {
            if (button.enabled) { auto action = button.action; action(); }
            return;
        }
    if (dialog) return;
    if (scene == Scene::Battle) { battleClick(p); return; }
    if (scene != Scene::Explore) return;
    if (walkStep < walkRoute.size()) return;
    auto cell = cellAt(p, MapOrigin, MapTile, MapW, MapH);
    // Characters extend above their foot tile; clicking their head or name must also interact.
    for (const auto& e : Events)
        if (eventVisible(e))
        {
            const auto at = worldCenter({e.x, e.y});
            if (sf::FloatRect(at - sf::Vector2f{18, 31}, {36, 66}).contains(p))
            { cell = sf::Vector2i{e.x, e.y}; break; }
        }
    if (!cell || !worldWalkable(*cell)) { notice = "나무, 바위, 물, 건물은 통과할 수 없습니다."; return; }
    pendingEvent = -1;
    for (int i = 0; i < static_cast<int>(Events.size()); ++i)
        if (eventVisible(Events[i]) && sf::Vector2i{Events[i].x, Events[i].y} == *cell) { pendingEvent = i; break; }
    walkRoute = worldPath(progress.cell, *cell); walkStep = 0;
    if (progress.cell != *cell && walkRoute.empty()) { pendingEvent = -1; notice = "지금은 도달할 수 없는 칸입니다."; }
    if (progress.cell == *cell && pendingEvent >= 0) { const int event = pendingEvent; pendingEvent = -1; interact(event); }
}
void Game::key(sf::Keyboard::Key code)
{
    if (dialog)
    {
        if (code == sf::Keyboard::Key::Enter || code == sf::Keyboard::Key::Space) advanceDialog();
        if (code == sf::Keyboard::Key::Escape && dialog->choice) dialog.reset();
        return;
    }
    if (scene == Scene::Explore)
    {
        if (code == sf::Keyboard::Key::G) grid = !grid;
        if (walkStep < walkRoute.size()) return;
        if (code == sf::Keyboard::Key::B) scene = Scene::Roster;
        if (code == sf::Keyboard::Key::J) scene = Scene::Journal;
        if (code == sf::Keyboard::Key::F5)
            notice = saveProgress(savePath, progress) ? "여행을 저장했습니다." : "저장 실패: 폴더 권한을 확인해 주세요.";
        if (code == sf::Keyboard::Key::F9)
            showDialog("여행 불러오기", {"현재 진행 대신 마지막으로 저장한 여행을 불러옵니다."}, [this] { restore(); }, "불러오기", true);
        if (code == sf::Keyboard::Key::Escape)
            showDialog("타이틀로", {"현재 여행을 저장하고 타이틀로 돌아갑니다."}, [this] { checkpoint(); scene = Scene::Title; }, "저장 후 돌아가기", true);
    }
    else if (scene == Scene::Roster || scene == Scene::Journal)
    { if (code == sf::Keyboard::Key::Escape || code == sf::Keyboard::Key::B || code == sf::Keyboard::Key::J) scene = Scene::Explore; }
    else if (scene == Scene::Battle && !enemyTurn)
    {
        if (code == sf::Keyboard::Key::Num1) command = Command::Move;
        if (code == sf::Keyboard::Key::Num2) command = Command::Attack;
        if (code == sf::Keyboard::Key::Num3) command = Command::Skill;
        if (code == sf::Keyboard::Key::Num4 && selected < static_cast<int>(actors.size())) guard(selected);
        if (code == sf::Keyboard::Key::Num5 && selected < static_cast<int>(actors.size())) drinkPotion(selected);
        if (code == sf::Keyboard::Key::Space) endPlayerTurn();
        if (code == sf::Keyboard::Key::Escape)
            showDialog("전투 중단", {"전투 전 탐험으로 돌아갑니다. 이야기 진행과 동료는 잃지 않습니다."},
                [this] { scene = Scene::Explore; buildRegion(); }, "탐험으로 돌아가기", true);
    }
}
void Game::battleClick(sf::Vector2f p)
{
    if (enemyTurn || selected >= static_cast<int>(actors.size())) return;
    const auto cell = cellAt(p, BattleOrigin, BattleTile, BattleW, BattleH); if (!cell) return;
    const int target = actorAt(*cell);
    if (target >= 0 && !actors[target].enemy)
    {
        if (command == Command::Skill)
        {
            const auto skill = actors[selected].type == -1 ? Skill::Mend : Monsters[actors[selected].type].skill;
            if (skill == Skill::Mend || skill == Skill::Pollen || skill == Skill::Ward || skill == Skill::Rally)
            { useSkill(selected, *cell); return; }
        }
        selected = target;
        return;
    }
    if (command == Command::Move) moveActor(selected, *cell);
    if (command == Command::Attack && (target < 0 || !attack(selected, target)))
        record("공격할 적을 사거리 안에서 선택해 주세요. 장애물은 시야를 막습니다.");
    if (command == Command::Skill && !useSkill(selected, *cell))
        record("기술의 대상, 사거리, MP와 행동 여부를 확인해 주세요.");
}

void Game::label(sf::RenderTarget& out, const std::string& value, sf::Vector2f at, unsigned size, sf::Color color) const
{
    if (!fontReady) return;
    sf::Text t(font, utf8(value), size); t.setPosition(at); t.setFillColor(color); out.draw(t);
}
float Game::wrapped(sf::RenderTarget& out, const std::string& value, sf::Vector2f at,
                    float width, unsigned size, sf::Color color) const
{
    if (!fontReady) return at.y;
    sf::String line;
    float advance = 0, y = at.y;
    const auto content = utf8(value);
    for (auto c : content)
    {
        const float next = font.getGlyph(c, size, false).advance;
        if (c == '\n' || (advance + next > width && !line.isEmpty()))
        {
            sf::Text t(font, line, size); t.setPosition({at.x, y}); t.setFillColor(color); out.draw(t);
            y += size * 1.65f; line.clear(); advance = 0;
        }
        if (c != '\n') { line += c; advance += next; }
    }
    if (!line.isEmpty())
    { sf::Text t(font, line, size); t.setPosition({at.x, y}); t.setFillColor(color); out.draw(t); y += size * 1.65f; }
    return y;
}
void Game::addButton(sf::RenderTarget& out, sf::FloatRect rect, std::string value,
                     std::function<void()> action, bool enabled, bool active)
{
    box(out, rect.position, rect.size, !enabled ? sf::Color{37, 47, 47} : active ? sf::Color{63, 115, 100} : sf::Color{46, 72, 68});
    box(out, rect.position, {rect.size.x, 2}, active ? sf::Color{203, 204, 146} : sf::Color{84, 113, 95});
    label(out, value, rect.position + sf::Vector2f{12, 10}, 17, enabled ? sf::Color{231, 233, 213} : sf::Color{109, 122, 115});
    buttons.push_back({rect, std::move(value), std::move(action), enabled, active});
}

namespace
{
void tree(sf::RenderTarget& out, sf::Vector2f p, float s = 1)
{
    oval(out, p + sf::Vector2f{2, 11} * s, {20 * s, 8 * s}, {15, 36, 29, 130});
    box(out, p + sf::Vector2f{-3, -7} * s, {7 * s, 25 * s}, {105, 77, 47});
    oval(out, p + sf::Vector2f{-9, -9} * s, {17 * s, 16 * s}, {30, 81, 52});
    oval(out, p + sf::Vector2f{10, -11} * s, {17 * s, 17 * s}, {40, 99, 61});
    oval(out, p + sf::Vector2f{0, -23} * s, {20 * s, 18 * s}, {52, 117, 69});
    oval(out, p + sf::Vector2f{-4, -29} * s, {11 * s, 8 * s}, {77, 141, 79});
}
void portrait(sf::RenderTarget& out, sf::Vector2f p, int type, sf::Color color, float scale = 1, float bob = 0)
{
    auto v = [scale](float x, float y) { return sf::Vector2f{x * scale, y * scale}; };
    oval(out, p + v(0, 12), v(15, 6), {12, 27, 28, 140}); p.y += bob;
    if (type == -1 || type == -2)
    {
        box(out, p + v(-8, 3), v(6, 12), {50, 46, 43}); box(out, p + v(3, 3), v(6, 12), {50, 46, 43});
        box(out, p + v(-11, -13), v(22, 22), color);
        oval(out, p + v(0, -19), v(10, 12), type == -2 ? sf::Color{122, 129, 125} : sf::Color{59, 72, 68});
        oval(out, p + v(0, -17), v(7, 8), {235, 195, 148});
        box(out, p + v(-9, -28), v(18, 6), type == -2 ? sf::Color{146, 151, 142} : sf::Color{75, 61, 49});
        box(out, p + v(12, -15), v(3, 30), {162, 124, 74});
        if (type == -1) box(out, p + v(-12, -10), v(24, 4), {223, 189, 105});
    }
    else if (type == 0)
    {
        oval(out, p + v(0, -1), v(17, 14), color); oval(out, p + v(-7, -9), v(5, 3), {192, 235, 214});
    }
    else
    {
        if (type == 1 || type == 4 || type == 8)
        {
            oval(out, p + v(-14, -7), v(14, 18), color); oval(out, p + v(14, -7), v(14, 18), color);
            oval(out, p + v(-15, -8), v(6, 9), {225, 223, 187, 150}); oval(out, p + v(15, -8), v(6, 9), {225, 223, 187, 150});
        }
        if (type == 7)
            for (int n = 0; n < 3; ++n)
            { box(out, p + v(-24, -5.f + n * 6), v(48, 2), color); }
        oval(out, p, v(type == 3 ? 16.f : 11.f, 14), color);
        oval(out, p + v(0, -17), v(type == 3 ? 14.f : 10.f, 11), color);
        if (type == 2 || type == 9 || type == 8)
        {
            sf::CircleShape ear(7 * scale, 3); ear.setFillColor(color);
            ear.setPosition(p + v(-14, -34)); out.draw(ear); ear.setPosition(p + v(1, -34)); out.draw(ear);
        }
        if (type == 5) { oval(out, p + v(-10, -29), v(11, 5), {93, 162, 83}); oval(out, p + v(9, -31), v(10, 6), {145, 190, 103}); }
        if (type == 6) { oval(out, p + v(15, 6), v(8, 5), {233, 163, 79}); oval(out, p + v(20, 1), v(4, 7), {243, 208, 115}); }
        if (type == 9) oval(out, p + v(15, 4), v(10, 8), {236, 205, 156});
    }
    oval(out, p + v(-4, type == 0 ? -2.f : -18.f), v(1.6f, 2), {33, 44, 43});
    oval(out, p + v(4, type == 0 ? -2.f : -18.f), v(1.6f, 2), {33, 44, 43});
}
}

void Game::drawWorld(sf::RenderTarget& out, sf::Vector2f mouse)
{
    label(out, "1장 · 숲 밖의 식탁", {28, 22}, 28);
    label(out, std::string(RegionNames[progress.map]) + "  /  22 × 14", {30, 67}, 17, {149, 177, 151});
    label(out, "루안 · 인간 남성 · Lv." + std::to_string(level()), {560, 68}, 17);
    for (int y = 0; y < MapH; ++y)
        for (int x = 0; x < MapW; ++x)
        {
            auto p = worldCenter({x, y}); char t = terrain[y][x]; int seed = (x * 11 + y * 17) % 7;
            sf::Color color{static_cast<std::uint8_t>(58 + seed), static_cast<std::uint8_t>(87 + seed * 2), 57};
            if (progress.map == 2) color = {77, 101, 67};
            if (t == ':') color = progress.map == 2 ? sf::Color{138, 131, 108} : sf::Color{130, 116, 78};
            if (t == '~') color = {47, 103, 114};
            box(out, p - sf::Vector2f{20, 20}, {40, 40}, color);
            if (grid) { box(out, p - sf::Vector2f{20, 20}, {40, 1}, {188, 207, 157, 25}); box(out, p - sf::Vector2f{20, 20}, {1, 40}, {188, 207, 157, 25}); }
            if (t == '~') box(out, p + sf::Vector2f{-12, static_cast<float>(seed - 3)}, {20, 2}, {101, 158, 159});
            if (t == '.' && seed == 3) { oval(out, p + sf::Vector2f{9, 5}, {2, 2}, {212, 190, 109}); box(out, p + sf::Vector2f{-9, -7}, {2, 5}, {105, 136, 73}); }
        }
    auto hover = cellAt(mouse, MapOrigin, MapTile, MapW, MapH);
    auto route = walkStep < walkRoute.size() ? std::vector<sf::Vector2i>(walkRoute.begin() + walkStep, walkRoute.end()) :
        hover && worldWalkable(*hover) ? worldPath(progress.cell, *hover) : std::vector<sf::Vector2i>{};
    for (auto cell : route) oval(out, worldCenter(cell), {2.8f, 2.8f}, {185, 224, 181, 180});
    if (hover)
    {
        sf::RectangleShape highlight({36, 36}); highlight.setPosition(worldCenter(*hover) - sf::Vector2f{18, 18});
        highlight.setFillColor(sf::Color::Transparent); highlight.setOutlineThickness(2);
        highlight.setOutlineColor(worldWalkable(*hover) ? sf::Color{226, 221, 149} : sf::Color{215, 111, 99}); out.draw(highlight);
    }
    const int heroRow = std::clamp(static_cast<int>((heroPosition.y - MapOrigin.y) / MapTile), 0, MapH - 1);
    for (int y = 0; y < MapH; ++y)
    {
        for (int x = 0; x < MapW; ++x)
        {
            auto p = worldCenter({x, y}); char t = terrain[y][x];
            if (t == 'T') tree(out, p, .78f);
            if (t == 'R') { oval(out, p + sf::Vector2f{0, 4}, {16, 10}, {98, 112, 96}); oval(out, p + sf::Vector2f{-3, 0}, {11, 6}, {145, 154, 129}); }
            if (t == 'H')
            {
                box(out, p + sf::Vector2f{-20, -8}, {40, 28}, {163, 141, 105});
                box(out, p + sf::Vector2f{-20, -19}, {40, 15}, {121, 77, 60});
                box(out, p + sf::Vector2f{-5, 1}, {10, 19}, {80, 71, 56});
            }
        }
        for (const auto& e : Events)
        {
            if (e.y != y || !eventVisible(e)) continue;
            const auto p = worldCenter({e.x, e.y});
            if (e.kind == EventKind::Monster) portrait(out, p, e.value, Monsters[e.value].color, .85f);
            else if (e.kind == EventKind::Elder || e.kind == EventKind::Archivist || e.kind == EventKind::Cook)
                portrait(out, p, e.kind == EventKind::Elder ? 3 : -1,
                    e.kind == EventKind::Cook ? sf::Color{206, 171, 118} : sf::Color{159, 167, 188}, .85f);
            else if (e.kind == EventKind::Herb)
            { oval(out, p, {10, 5}, {103, 204, 158}); oval(out, p + sf::Vector2f{0, -5}, {5, 9}, {122, 221, 178}); }
            else if (e.kind == EventKind::Battle)
            {
                box(out, p + sf::Vector2f{-2, -22}, {4, 38}, {172, 152, 100});
                box(out, p + sf::Vector2f{2, -23}, {22, 16}, progress.battleMask & (1 << e.value) ? sf::Color{101, 159, 126} : sf::Color{184, 101, 83});
            }
            else if (e.kind == EventKind::Exit)
            { box(out, p + sf::Vector2f{-12, -6}, {24, 12}, {201, 181, 115}); box(out, p + sf::Vector2f{-2, -4}, {4, 21}, {125, 101, 64}); }
            else if (e.kind == EventKind::Shrine)
            { box(out, p + sf::Vector2f{-13, -17}, {26, 31}, {139, 153, 128}); label(out, "◇", p + sf::Vector2f{-9, -20}, 20, {77, 121, 103}); }
            else { box(out, p + sf::Vector2f{-9, -6}, {18, 13}, {230, 213, 164}); }
            if (e.kind != EventKind::Herb && e.kind != EventKind::Exit)
            {
                const auto len = utf8(e.label).getSize(); const float width = static_cast<float>(len) * 12.f + 8;
                box(out, p + sf::Vector2f{-width / 2, 19}, {width, 19}, {20, 38, 33, 225});
                label(out, e.label, p + sf::Vector2f{-width / 2 + 4, 19}, 12);
            }
        }
        if (y == heroRow) portrait(out, heroPosition, -1, {88, 166, 158}, .85f,
            walkStep < walkRoute.size() ? std::sin(elapsed * 16) * 2 : 0);
    }
    box(out, {932, 125}, {400, 560}, {26, 46, 40});
    label(out, "현재 목표", {954, 144}, 22, {221, 203, 136});
    wrapped(out, Objectives[progress.stage], {954, 186}, 350, 19);
    label(out, "여행 수첩", {954, 287}, 20);
    const int recruited = static_cast<int>(std::count(progress.recruited.begin(), progress.recruited.end(), true));
    label(out, "동료 " + std::to_string(recruited) + " / 10    출전 " + std::to_string(progress.party.size() + 1) + " / 5", {954, 328}, 17);
    label(out, "푸른 약초 " + std::to_string(progress.herbs) + "개    공통 Lv." + std::to_string(level()), {954, 362}, 17);
    wrapped(out, "몬스터·주민·표식을 클릭하면 걸어가서 대화합니다. 약초와 편지는 선택 영입에만 필요합니다.", {954, 418}, 350, 17, {170, 193, 171});
    if (hover)
        for (const auto& e : Events)
            if (eventVisible(e) && sf::Vector2i{e.x, e.y} == *hover)
            {
                label(out, std::string("선택: ") + e.label, {954, 544}, 18, {225, 206, 142});
                if (e.kind == EventKind::Monster) wrapped(out, Monsters[e.value].requirement, {954, 579}, 350, 16);
            }
    const bool idle = walkStep >= walkRoute.size();
    addButton(out, {{28, 715}, {185, 48}}, "거점 · 편성 [B]", [this] { scene = Scene::Roster; }, idle);
    addButton(out, {{225, 715}, {175, 48}}, "여행 기록 [J]", [this] { scene = Scene::Journal; }, idle);
    addButton(out, {{412, 715}, {150, 48}}, "저장 [F5]", [this] { key(sf::Keyboard::Key::F5); }, idle);
    addButton(out, {{574, 715}, {175, 48}}, "불러오기 [F9]", [this] { key(sf::Keyboard::Key::F9); }, idle);
    addButton(out, {{761, 715}, {147, 48}}, "격자 [G]", [this] { grid = !grid; });
    wrapped(out, notice.empty() ? "표식: 붉은 깃발은 전투, 나무 표지판은 지역 이동입니다. 사건 후 자동 저장됩니다." : notice,
        {30, 790}, 1240, 18, {189, 208, 177});
    label(out, "탐험 중에는 이동력 제한이 없습니다. 이동 중에는 목적지 도착 후 다음 명령을 내립니다.", {30, 850}, 15, {130, 158, 138});
}

void Game::drawTitle(sf::RenderTarget& out)
{
    for (int i = 0; i < 18; ++i) tree(out, {50.f + i * 77, 620.f + std::sin(static_cast<float>(i)) * 35}, 2.1f);
    label(out, "숲 밖의 식탁", {165, 135}, 54, {232, 226, 184});
    label(out, "MONSTER TACTICS  /  CHAPTER 01", {169, 218}, 20, {139, 181, 151});
    wrapped(out, "몬스터들에게 자란 인간 청년 루안.\n어머니의 흔적을 찾는 여행은, 낯선 이들과 한 식탁에 앉는 일에서 시작된다.", {170, 280}, 770, 22);
    addButton(out, {{170, 428}, {265, 53}}, "새 여행 시작", [this] {
        if (std::filesystem::exists(savePath)) showDialog("새 여행", {"기존 저장을 새 여행으로 교체합니다. 이전 여행은 chapter1.previous.sav에 따로 보관됩니다."}, [this] { newGame(); }, "새로 시작", true);
        else newGame();
    });
    addButton(out, {{453, 428}, {265, 53}}, "여행 이어가기", [this] { restore(); }, loadProgress(savePath).has_value());
    addButton(out, {{736, 428}, {180, 53}}, "종료", [this] { closeRequested = true; });
    label(out, "테스트 버전 · 자유 영입 10명 · 최대 5인 편성 · 전술 전투 3개", {171, 528}, 18, {173, 193, 163});
    wrapped(out, "조작: 마우스 왼쪽 클릭 / 대화 Space·Enter / 거점 B / 기록 J / 저장 F5 / 불러오기 F9", {170, 762}, 1020, 17);
    label(out, notice, {170, 822}, 17, {218, 171, 132});
}
void Game::drawDialog(sf::RenderTarget& out)
{
    if (!dialog) return;
    box(out, {0, 0}, {static_cast<float>(ScreenW), static_cast<float>(ScreenH)}, {8, 17, 17, 215});
    box(out, {158, 168}, {1044, 570}, {31, 52, 45});
    box(out, {158, 168}, {1044, 4}, {195, 183, 128});
    label(out, dialog->title, {204, 202}, 29, {231, 217, 162});
    wrapped(out, dialog->pages[dialog->page], {204, 274}, 945, 22);
    label(out, std::to_string(dialog->page + 1) + " / " + std::to_string(dialog->pages.size()), {205, 675}, 16, {145, 178, 151});
    buttons.clear();
    const bool last = dialog->page + 1 == dialog->pages.size();
    addButton(out, {{825, 654}, {325, 51}}, last ? dialog->accept : "계속 [Space / Enter]", [this] { advanceDialog(); });
    if (dialog->choice)
        addButton(out, {{584, 654}, {225, 51}}, "다음에 결정 [Esc]", [this] { dialog.reset(); });
}

void Game::drawRoster(sf::RenderTarget& out)
{
    label(out, "여행자의 거점", {28, 22}, 29);
    label(out, "루안은 고정 출전 · 동료는 최대 4명 · 대기 동료도 같은 레벨로 성장합니다.", {30, 70}, 18, {172, 198, 171});
    label(out, "공통 Lv." + std::to_string(level()) + "   출전 " + std::to_string(progress.party.size() + 1) + " / 5", {960, 32}, 22, {221, 206, 144});
    for (int id = 0; id < 10; ++id)
    {
        const auto& m = Monsters[id];
        const float x = 28.f + (id / 5) * 662.f, y = 122.f + (id % 5) * 130.f;
        const bool owned = progress.recruited[id];
        const bool active = std::find(progress.party.begin(), progress.party.end(), id) != progress.party.end();
        box(out, {x, y}, {638, 117}, owned ? sf::Color{31, 54, 46} : sf::Color{31, 41, 39});
        portrait(out, {x + 38, y + 59}, id, m.color, 1.15f);
        label(out, std::string(m.name) + " · " + m.species, {x + 78, y + 8}, 19, owned ? sf::Color{225, 228, 200} : sf::Color{154, 173, 153});
        label(out, m.role, {x + 78, y + 37}, 15, {170, 196, 167});
        if (owned)
        {
            const auto a = makeAlly(id, {});
            label(out, "HP " + std::to_string(a.maxHp) + "  공격 " + std::to_string(a.attack) + "  이동 " + std::to_string(a.movement), {x + 78, y + 62}, 14);
            label(out, std::string(m.technique) + " · " + m.effect, {x + 78, y + 87}, 13, {193, 193, 157});
        }
        else
        {
            label(out, std::string(RegionNames[m.map]) + " / " + m.requirement, {x + 78, y + 67}, 14);
            label(out, "만남과 계약은 선택입니다.", {x + 78, y + 91}, 12, {123, 149, 130});
        }
        addButton(out, {{x + 488, y + 12}, {135, 42}}, owned ? (active ? "출전 → 대기" : "대기 → 출전") : "미계약",
            [this, id] { toggleParty(id); }, owned, active);
    }
    addButton(out, {{28, 800}, {220, 49}}, "탐험으로 [Esc]", [this] { scene = Scene::Explore; });
    wrapped(out, notice.empty() ? "전투에 들어가면 현재 편성으로 출전합니다. 전투 중 교체는 없습니다." : notice,
        {275, 810}, 1040, 17, {186, 207, 173});
}
void Game::drawJournal(sf::RenderTarget& out)
{
    label(out, "여행 기록", {36, 28}, 30);
    box(out, {36, 112}, {1260, 115}, {34, 57, 46});
    label(out, "다음 목적", {60, 128}, 21, {222, 206, 144});
    wrapped(out, Objectives[progress.stage], {60, 171}, 1190, 21);
    const std::array<const char*, 6> steps{
        "숲에서 출발 · 목걸이를 들고 바람길의 검문소로",
        "열린 길 · 돌여울 마을의 기록원 이안과 대화",
        "낯선 마을 · 압수 수레에서 주민들의 식량 되찾기",
        "운송표의 행선지 · 바람길 북동쪽 보급 창고 조사",
        "장부와 증언 · 마을 기록원에게 원본 전달",
        "첫 번째 식탁 · 어머니의 다음 흔적은 북부 우편소"};
    for (int i = 0; i < 6; ++i)
    {
        const std::string mark = i < progress.stage ? "완료" : i == progress.stage ? "진행" : "예정";
        label(out, mark + "  " + steps[i], {64, 267.f + i * 56}, 20,
            i <= progress.stage ? sf::Color{214, 225, 193} : sf::Color{113, 141, 124});
    }
    wrapped(out, "선택 활동: 숲의 돌제단 조사, 푸른 약초 수집, 바람길의 편지 발견, 요리사 마레와 대화.\n이 활동들은 동료 영입을 돕지만 메인 이야기의 필수 조건은 아닙니다.", {64, 635}, 1180, 19);
    addButton(out, {{36, 806}, {230, 50}}, "탐험으로 [Esc]", [this] { scene = Scene::Explore; });
    label(out, "장소 이동과 동료 영입, 사건 해결 후 자동 저장됩니다.", {302, 817}, 17, {155, 186, 155});
}
void Game::drawBattle(sf::RenderTarget& out, sf::Vector2f mouse)
{
    label(out, BattleNames[battleId], {34, 22}, 29);
    label(out, "적 전원 제압 / 아군 전원 전투 불능 시 패배 / 영구 사망 없음", {36, 70}, 17, {166, 193, 160});
    label(out, std::to_string(round) + "턴 · " + (enemyTurn ? "적 진영" : "아군 진영"), {1040, 32}, 25,
        enemyTurn ? sf::Color{229, 159, 128} : sf::Color{178, 219, 170});
    const auto hover = cellAt(mouse, BattleOrigin, BattleTile, BattleW, BattleH);
    const Actor* selectedActor = selected < static_cast<int>(actors.size()) ? &actors[selected] : nullptr;
    for (int y = 0; y < BattleH; ++y)
        for (int x = 0; x < BattleW; ++x)
        {
            const sf::Vector2i cell{x, y}; auto p = battleCenter(cell);
            const bool blocked = field[y][x] == '#';
            sf::Color color = (x + y) % 2 ? sf::Color{88, 104, 75} : sf::Color{95, 111, 80};
            if (battleId == 1) color = (x + y) % 2 ? sf::Color{125, 120, 96} : sf::Color{134, 128, 103};
            if (battleId == 2) color = (x + y) % 2 ? sf::Color{98, 94, 79} : sf::Color{110, 104, 87};
            if (selectedActor && selectedActor->alive() && !enemyTurn)
            {
                if (command == Command::Move && !selectedActor->moved && !selectedActor->rooted && openCell(cell, selected))
                {
                    const auto route = battlePath(selectedActor->cell, cell, selected);
                    if (!route.empty() && static_cast<int>(route.size()) <= selectedActor->movement) color = {72, 125, 107};
                }
                if (command == Command::Attack && !selectedActor->acted && inRange(*selectedActor, cell, selectedActor->range)) color = {142, 109, 77};
                if (command == Command::Skill && !selectedActor->acted)
                {
                    Skill skill = selectedActor->type == -1 ? Skill::Mend : Monsters[selectedActor->type].skill;
                    const int range = skill == Skill::Fang ? 1 : (skill == Skill::Pollen || skill == Skill::Root || skill == Skill::Venom ? 4 : 3);
                    if (inRange(*selectedActor, cell, skill == Skill::Ward || skill == Skill::Rally ? 2 : range)) color = {96, 113, 145};
                }
            }
            box(out, p - sf::Vector2f{30, 30}, {60, 60}, blocked ? sf::Color{65, 75, 61} : color);
            if (blocked)
            {
                if (battleId == 0) tree(out, p, 1.1f);
                else { box(out, p + sf::Vector2f{-20, -20}, {40, 40}, {128, 98, 61}); box(out, p + sf::Vector2f{-17, -3}, {34, 4}, {165, 134, 84}); }
            }
            if (hover && *hover == cell)
            {
                sf::RectangleShape outline({56, 56}); outline.setPosition(p - sf::Vector2f{28, 28});
                outline.setFillColor(sf::Color::Transparent); outline.setOutlineThickness(2); outline.setOutlineColor({231, 218, 147}); out.draw(outline);
            }
        }
    for (int y = 0; y < BattleH; ++y)
        for (int i = 0; i < static_cast<int>(actors.size()); ++i)
        {
            const auto& a = actors[i]; if (!a.alive() || a.cell.y != y) continue;
            const auto p = battleCenter(a.cell);
            if (selected == i && !enemyTurn) oval(out, p + sf::Vector2f{0, 15}, {25, 9}, {220, 207, 128});
            portrait(out, p, a.type, a.color, 1.1f);
            box(out, p + sf::Vector2f{-24, 24}, {48, 5}, {35, 45, 37});
            box(out, p + sf::Vector2f{-24, 24}, {48.f * a.hp / a.maxHp, 5}, a.enemy ? sf::Color{214, 125, 100} : sf::Color{131, 199, 153});
            label(out, std::to_string(a.hp), p + sf::Vector2f{-9, 30}, 12);
            if (a.poison || a.rooted || a.shield || a.weakened || a.boosted)
                label(out, a.poison ? "독" : a.rooted ? "묶임" : a.shield ? "보호" : a.weakened ? "약화" : "강화", p + sf::Vector2f{-15, -43}, 12, {236, 206, 137});
        }
    box(out, {810, 133}, {516, 558}, {26, 44, 39});
    label(out, "출전 동료 · 클릭하여 선택", {830, 148}, 20);
    int row = 0;
    for (int i = 0; i < static_cast<int>(actors.size()); ++i)
    {
        const auto& a = actors[i]; if (a.enemy) continue;
        const float y = 190.f + row++ * 61.f;
        addButton(out, {{830, y}, {477, 53}}, a.name + "   HP " + std::to_string(a.hp) + "/" + std::to_string(a.maxHp) +
            "   MP " + std::to_string(a.mp) + "   " + (a.acted ? "행동 완료" : "행동 가능"),
            [this, i] { selected = i; command = Command::Move; }, a.alive() && !enemyTurn, selected == i);
    }
    if (selectedActor && selectedActor->alive())
    {
        const auto& a = *selectedActor;
        label(out, a.name + "  이동 " + std::to_string(a.movement) + " / 사거리 " + std::to_string(a.range) +
            " / 공격 " + std::to_string(a.attack), {830, 510}, 17);
        label(out, std::string("이동 ") + (a.moved ? "완료" : "가능") + " · 행동 " + (a.acted ? "완료" : "가능"), {830, 542}, 16, {193, 209, 173});
        wrapped(out, a.type == -1 ? "길잡이의 빛 · MP 3\n사거리 3 / 자신 또는 아군 체력 16 회복" :
            std::string(Monsters[a.type].technique) + " · MP " + std::to_string(Monsters[a.type].cost) + "\n" + Monsters[a.type].effect,
            {830, 576}, 467, 17, {192, 194, 221});
        label(out, "보호·지원 기술은 시전자를 클릭해 사용합니다.", {830, 653}, 14, {150, 178, 153});
    }
    const bool ready = !enemyTurn && selectedActor && selectedActor->alive();
    addButton(out, {{34, 723}, {139, 49}}, "1 이동", [this] { command = Command::Move; }, ready && !selectedActor->moved, command == Command::Move);
    addButton(out, {{182, 723}, {139, 49}}, "2 공격", [this] { command = Command::Attack; }, ready && !selectedActor->acted, command == Command::Attack);
    addButton(out, {{330, 723}, {139, 49}}, "3 기술", [this] { command = Command::Skill; }, ready && !selectedActor->acted, command == Command::Skill);
    addButton(out, {{478, 723}, {139, 49}}, "4 방어", [this] { guard(selected); }, ready && !selectedActor->acted);
    addButton(out, {{626, 723}, {190, 49}}, "5 물약 (" + std::to_string(potions) + ")", [this] { drinkPotion(selected); }, ready && !selectedActor->acted && potions > 0);
    addButton(out, {{830, 723}, {232, 49}}, "턴 종료 [Space]", [this] { endPlayerTurn(); }, !enemyTurn);
    addButton(out, {{1077, 723}, {249, 49}}, "철수 / 재편성 [Esc]", [this] { key(sf::Keyboard::Key::Escape); }, !enemyTurn);
    const std::size_t first = log.size() > 3 ? log.size() - 3 : 0;
    for (std::size_t i = first; i < log.size(); ++i) label(out, log[i], {36, 795.f + (i - first) * 29.f}, 16, {181, 204, 176});
}
void Game::drawResult(sf::RenderTarget& out)
{
    label(out, victory ? "길이 다시 열렸다" : "잠시 물러날 때", {170, 182}, 42, {230, 217, 163});
    wrapped(out, victory ? "전투에서 승리했습니다.\n모든 동료가 회복되며, 처음 해결한 사건이면 대기 동료를 포함한 공통 레벨이 오릅니다." :
        "아군이 모두 전투 불능이 되었습니다. 영구 사망이나 동료 손실은 없습니다.\n같은 편성으로 재도전하거나 탐험으로 돌아가 다른 동료를 편성해 보세요.", {174, 285}, 970, 23);
    addButton(out, {{174, 482}, {350, 57}}, victory ? "사건의 다음으로" : "같은 편성으로 재도전", [this] { finishBattle(); });
    if (!victory) addButton(out, {{546, 482}, {350, 57}}, "거점에서 다시 준비", [this] { scene = Scene::Roster; buildRegion(); });
}
void Game::drawEnding(sf::RenderTarget& out)
{
    label(out, "1장 완료 · 숲 밖의 식탁", {160, 145}, 42, {233, 218, 164});
    wrapped(out, "어머니의 흔적은 북부 우편소로 이어졌다.\n제국의 억압은 여전히 남아 있지만, 돌여울에는 서로의 이름을 부르는 작은 식탁이 생겼다.\n\n루안의 여행은 이제 시작이다.", {165, 263}, 1000, 24);
    label(out, "함께한 동료 " + std::to_string(std::count(progress.recruited.begin(), progress.recruited.end(), true)) + " / 10   ·   공통 Lv." + std::to_string(level()), {165, 487}, 22);
    addButton(out, {{165, 576}, {340, 56}}, "계속 탐험 / 동료 수집", [this] { scene = Scene::Explore; notice = "남은 동료를 만나거나 해결한 전투를 다시 시험할 수 있습니다."; });
    addButton(out, {{528, 576}, {300, 56}}, "타이틀로", [this] { scene = Scene::Title; });
    label(out, "2장 이후는 아직 포함되지 않은 1장 테스트 버전입니다.", {166, 714}, 18, {157, 186, 157});
}
void Game::render(sf::RenderTarget& out, sf::Vector2f mouse)
{
    buttons.clear(); out.clear({18, 33, 29});
    switch (scene)
    {
    case Scene::Title: drawTitle(out); break;
    case Scene::Explore: drawWorld(out, mouse); break;
    case Scene::Roster: drawRoster(out); break;
    case Scene::Journal: drawJournal(out); break;
    case Scene::Battle: drawBattle(out, mouse); break;
    case Scene::Result: drawResult(out); break;
    case Scene::Ending: drawEnding(out); break;
    }
    if (dialog) drawDialog(out);
}

void Game::autoPlayerTurn()
{
    // Test driver uses the same movement, attacks and skills as the player UI.
    for (int who = 0; who < static_cast<int>(actors.size()) && scene == Scene::Battle; ++who)
    {
        auto& a = actors[who]; if (a.enemy || !a.alive()) continue;
        const Skill skill = a.type == -1 ? Skill::Mend : Monsters[a.type].skill;
        const bool healer = skill == Skill::Mend || skill == Skill::Pollen;
        if (healer)
        {
            int hurt = -1;
            for (int i = 0; i < static_cast<int>(actors.size()); ++i)
                if (!actors[i].enemy && actors[i].alive() && actors[i].hp < actors[i].maxHp * .58f &&
                    inRange(a, actors[i].cell, skill == Skill::Mend ? 3 : 4) &&
                    (hurt < 0 || actors[i].hp < actors[hurt].hp)) hurt = i;
            if (hurt >= 0 && useSkill(who, actors[hurt].cell)) continue;
        }
        if (a.hp < a.maxHp * .4f && drinkPotion(who)) continue;
        int target = -1, best = std::numeric_limits<int>::max(); sf::Vector2i destination = a.cell;
        for (int i = 0; i < static_cast<int>(actors.size()); ++i)
        {
            const auto& b = actors[i]; if (!b.enemy || !b.alive()) continue;
            for (int y = 0; y < BattleH; ++y)
                for (int x = 0; x < BattleW; ++x)
                {
                    const sf::Vector2i p{x, y};
                    if (!openCell(p, who) || distance(p, b.cell) > a.range || !sight(p, b.cell)) continue;
                    auto route = battlePath(a.cell, p, who);
                    if (p != a.cell && route.empty()) continue;
                    int score = static_cast<int>(route.size()) * 100 + b.hp;
                    if (score < best) { best = score; target = i; destination = p; }
                }
        }
        if (target < 0) { guard(who); continue; }
        const auto route = battlePath(a.cell, destination, who);
        if (!route.empty()) moveActor(who, route[std::min(a.movement, static_cast<int>(route.size())) - 1]);
        if (skill == Skill::Ward || skill == Skill::Rally)
        {
            if (round % 3 == 0 && distance(a.cell, actors[target].cell) <= 4 && useSkill(who, a.cell)) continue;
        }
        if (!healer && skill != Skill::Ward && skill != Skill::Rally && useSkill(who, actors[target].cell)) continue;
        if (!attack(who, target)) guard(who);
    }
}
void Game::preparePreview(const std::string& kind)
{
    previewMode = true;
    progress = {}; progress.stage = 2; progress.battleMask = 1; progress.herbs = 3;
    const bool showParty = kind == "roster" || kind == "battle" || kind == "ending";
    progress.recruited.fill(showParty);
    if (showParty) progress.party = {0, 1, 2, 3};
    else { progress.party.clear(); progress.stage = 0; progress.battleMask = 0; }
    progress.map = kind == "village" ? 2 : 0; progress.cell = {2, 7};
    if (kind == "village") { progress.stage = 2; progress.battleMask = 1; }
    scene = Scene::Explore; buildRegion();
    notice = "시제품: 대화, 계약, 편성, 전투, 저장과 1장 결말을 시험할 수 있습니다.";
    if (kind == "roster") scene = Scene::Roster;
    if (kind == "battle") { startBattle(1); selected = 0; }
    if (kind == "ending") { progress.stage = 5; progress.battleMask = 7; scene = Scene::Ending; }
    if (kind == "dialog")
        showDialog("숲의 어른", {"숲의 어른: 루안, 네 어머니의 이름은 세린이란다. 우리가 아는 건 이 목걸이와, 널 지키려 애썼던 모습뿐이야.\n\n돌여울 마을의 기록원은 오래된 여행 기록을 보관하고 있어. 사람들을 만나 보렴."});
}
void Game::runTests(const std::filesystem::path& scratch)
{
    savePath = scratch / "chapter1-test.sav";
    auto clearDialog = [this] { for (int n = 0; dialog && n < 20; ++n) advanceDialog(); require(!dialog, "dialog did not finish"); };
    auto eventId = [](EventKind kind, int value) {
        for (int i = 0; i < static_cast<int>(Events.size()); ++i)
            if (Events[i].kind == kind && Events[i].value == value) return i;
        throw std::runtime_error("event missing");
    };
    auto win = [this] {
        for (int turns = 0; scene == Scene::Battle && turns < 80; ++turns)
        {
            autoPlayerTurn();
            if (scene != Scene::Battle) break;
            endPlayerTurn();
            for (int i = 0; i < static_cast<int>(actors.size()) && scene == Scene::Battle; ++i)
                if (actors[i].enemy && actors[i].alive()) enemyAction(i);
            if (scene == Scene::Battle) beginPlayerTurn();
        }
        if (scene != Scene::Result || !victory)
        {
            for (const auto& line : log) std::cerr << line << '\n';
            throw std::runtime_error("combat simulation did not win encounter " + std::to_string(battleId));
        }
        std::cout << "PASS combat " << battleId << " party=" << progress.party.size() + 1 << " rounds=" << round << '\n';
    };
    for (int map = 0; map < 3; ++map)
    {
        progress.map = map; buildRegion();
        for (const auto& e : Events)
            if (e.map == map) require(!worldPath({2, 7}, {e.x, e.y}).empty(), "world event unreachable");
    }
    require(!cellAt({-1, -1}, MapOrigin, MapTile, MapW, MapH), "out-of-bounds click accepted");
    std::cout << "PASS all region event paths and bounds\n";

    newGame(); clearDialog();
    travel(1); interact(eventId(EventKind::Battle, 0)); clearDialog(); win(); finishBattle(); clearDialog();
    require(progress.stage == 1, "bridge progression failed");
    travel(2); interact(eventId(EventKind::Archivist, 0)); clearDialog(); require(progress.stage == 2, "archive progression failed");
    interact(eventId(EventKind::Battle, 1)); clearDialog(); win(); finishBattle(); clearDialog();
    travel(1); interact(eventId(EventKind::Battle, 2)); clearDialog(); win(); finishBattle(); clearDialog();
    travel(2); interact(eventId(EventKind::Archivist, 0)); clearDialog();
    require(progress.stage == 5 && scene == Scene::Ending, "chapter ending failed");
    require(progress.party.empty() && std::none_of(progress.recruited.begin(), progress.recruited.end(), [](bool x) { return x; }), "mandatory recruitment introduced");
    auto loaded = loadProgress(savePath);
    require(loaded && loaded->stage == 5 && loaded->battleMask == 7, "save round trip failed");
    std::cout << "PASS complete chapter without recruiting any companion; ending and save round trip\n";

    progress = {}; progress.battleMask = 1; progress.stage = 1; progress.flags = 511; progress.herbs = 6;
    for (int i = 0; i < 10; ++i) { require(canRecruit(i), "recruit condition failed"); recruit(i); }
    require(std::count(progress.recruited.begin(), progress.recruited.end(), true) == 10, "ten companions not recruited");
    require(progress.party.size() == 4 && progress.herbs == 4, "party cap or herb cost failed");
    progress.party = {0, 1, 2, 3}; startBattle(0); win(); finishBattle(); clearDialog();
    progress.party = {4, 5, 6, 7}; startBattle(1); win(); finishBattle(); clearDialog();
    progress.party = {6, 7, 8, 9}; startBattle(2); win(); finishBattle(); clearDialog();
    const int oldLevel = level(); startBattle(0); win(); finishBattle(); clearDialog();
    require(level() == oldLevel, "repeat battle grants duplicate progression");
    std::cout << "PASS all ten recruitments, max-five deployment, diverse parties, rematch rewards\n";

    progress = {};
    for (int type = -1; type < 10; ++type)
    {
        startBattle(0); actors.clear();
        Actor a = makeAlly(type, {2, 4});
        const Skill skill = type == -1 ? Skill::Mend : Monsters[type].skill;
        Actor b = makeAlly(0, {3, 4}); b.hp = 10;
        b.enemy = !(skill == Skill::Mend || skill == Skill::Pollen || skill == Skill::Ward || skill == Skill::Rally);
        Actor distant = makeAlly(0, {10, 8}); distant.enemy = true;
        actors = {a, b, distant};
        auto target = skill == Skill::Ward || skill == Skill::Rally ? a.cell : b.cell;
        require(useSkill(0, target), "skill not executable");
        require(actors[0].acted && actors[0].mp < a.mp, "skill action/MP not consumed");
        if (skill == Skill::Mend || skill == Skill::Pollen) require(actors[1].hp > 10, "healing failed");
        if (skill == Skill::Ward) require(actors[0].shield == 10, "ward failed");
        if (skill == Skill::Rally) require(actors[1].boosted > 0, "rally failed");
        if (skill == Skill::Root) require(actors[1].rooted == 1, "root failed");
        if (skill == Skill::Venom) require(actors[1].poison == 3, "poison failed");
        if (skill == Skill::Acid) require(actors[1].weakened > 0, "acid failed");
        if (skill == Skill::Gust) require(actors[1].cell == sf::Vector2i{4, 4}, "push failed");
    }
    std::cout << "PASS hero plus ten unique skills and resource costs\n";
    startBattle(0); actors = {makeAlly(-1, {1, 4}), makeAlly(0, {3, 4})}; actors[1].enemy = true;
    selected = 0; command = Command::Move;
    moveActor(0, {4, 1}); require(actors[0].cell == sf::Vector2i{1, 4}, "walked through obstacle");
    moveActor(0, {2, 4}); require(actors[0].moved, "move failed");
    moveActor(0, {1, 4}); require(actors[0].cell == sf::Vector2i{2, 4}, "moved twice in one turn");
    require(attack(0, 1), "basic attack failed"); require(!attack(0, 1), "attacked twice");
    std::ofstream invalid(scratch / "corrupt.sav"); invalid << "MONSTER_TACTICS_CH1 1\n99 0 0 0 0 2 7\n"; invalid.close();
    require(!loadProgress(scratch / "corrupt.sav"), "corrupt save accepted");
    std::cout << "PASS action economy, obstacles and corrupt-save handling\n";
    progress = {}; scene = Scene::Explore; dialog.reset(); buildRegion();
    walkRoute.clear(); walkStep = 0; buttons.clear();
    click(worldCenter({Monsters[0].x, Monsters[0].y}) - sf::Vector2f{0, 25});
    for (int n = 0; n < 200 && !dialog; ++n) update(.1f);
    require(dialog.has_value(), "click-to-walk interaction failed");
    dialog.reset();
    require(!progress.recruited[0], "declining dialogue recruited companion");
    click(worldCenter({Monsters[0].x, Monsters[0].y})); clearDialog();
    require(progress.recruited[0] && progress.party.size() == 1, "dialogue recruitment failed");
    std::cout << "PASS click-to-walk, event arrival, optional contract and dialogue recruitment\nALL TESTS PASSED\n";
}
}
