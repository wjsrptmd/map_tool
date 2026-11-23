#pragma once
#include <SFML/Graphics.hpp>
#include <array>
#include <string>
#include <vector>

namespace chapter
{
constexpr unsigned ScreenW = 1360, ScreenH = 900;
constexpr int MapW = 22, MapH = 14, BattleW = 12, BattleH = 9;
constexpr float MapTile = 40.f, BattleTile = 62.f;
constexpr sf::Vector2f MapOrigin{28, 125}, BattleOrigin{34, 133};

enum class Skill { Mend, Acid, Pollen, Fang, Ward, Gust, Root, Flame, Venom, Drain, Rally };
enum class Need { None, Herb, Victory, Shrine, Kindness, Letter };
struct Monster
{
    const char* name;
    const char* species;
    const char* role;
    const char* technique;
    const char* effect;
    const char* introduction;
    const char* motive;
    const char* requirement;
    Skill skill;
    Need need;
    int map, x, y, hp, attack, defense, move, range, mp, cost;
    sf::Color color;
};
inline const std::array<Monster, 10> Monsters{{
    {"모루", "이슬 슬라임", "원거리 · 약화", "산성 물방울", "사거리 3 / 피해 + 방어 약화",
     "모루: 숲 밖의 물은 전부 짜다는 소문이 있어. 직접 맛보기 전에는 믿을 수 없지!",
     "모루: 네 어머니를 찾는 여행에 함께할게. 대신 새로운 강을 만나면 잠깐 쉬어 가자.",
     "대화 후 자유롭게 계약", Skill::Acid, Need::None, 0, 7, 9, 34, 8, 2, 4, 3, 12, 3, {100, 194, 187}},
    {"리네", "달빛 나방족", "회복 · 원거리", "달빛 꽃가루", "사거리 4 / 아군 체력 18 회복",
     "리네: 피난민들에게 나눠 줄 연고를 만들고 있어. 푸른 약초 한 묶음만 가져다줄래?",
     "리네: 약초를 고마워. 숲 밖에도 치료가 필요한 이들이 많겠지. 나도 함께 가고 싶어.",
     "푸른 약초 1개 전달", Skill::Pollen, Need::Herb, 0, 15, 3, 27, 7, 1, 4, 3, 15, 3, {198, 175, 228}},
    {"가르", "잿빛 늑대족", "기동 · 근접 공격", "질풍의 송곳니", "근접 / 강력한 단일 공격",
     "가르: 길목을 막은 병사들 때문에 여행자들이 돌아오지 못해. 그 문제가 해결되기 전에는 떠날 수 없어.",
     "가르: 길을 다시 열어 줬군. 이제 내 두 눈으로 인간의 마을을 보고 싶다. 동등한 동료로 함께하지.",
     "첫 전투 승리", Skill::Fang, Need::Victory, 0, 18, 10, 38, 11, 2, 5, 1, 9, 3, {170, 179, 185}},
    {"도른", "이끼 골렘", "보호 · 근접", "숲의 성벽", "자신 주변 2칸 / 아군 보호막 10",
     "도른: 오래된 돌제단에 내 고향의 문양이 남아 있다. 그 글을 살펴보고 와 주겠니?",
     "도른: 기억은 돌 속에만 남겨 두는 게 아니구나. 새로운 길에서 내 이야기도 이어 가고 싶다.",
     "숲의 돌제단 조사", Skill::Ward, Need::Shrine, 0, 5, 3, 52, 8, 5, 3, 1, 12, 3, {126, 159, 110}},
    {"페아", "바람 하피", "원거리 · 밀치기", "날개 돌풍", "사거리 3 / 피해 + 한 칸 밀치기",
     "페아: 날 수 있다고 어디든 갈 수 있는 건 아니야. 제국의 감시탑을 피하려면 좋은 길잡이가 필요하거든.",
     "페아: 넌 종족보다 목적지를 먼저 물어보네. 마음에 들어. 서로의 길잡이가 되어 보자!",
     "대화 후 자유롭게 계약", Skill::Gust, Need::None, 1, 5, 3, 29, 9, 1, 5, 3, 12, 3, {131, 186, 220}},
    {"이오", "어린 나무정령", "제어 · 회복 보조", "뿌리 묶기", "사거리 4 / 피해 + 다음 이동 봉쇄",
     "이오: 인간과 함께 가도 괜찮을까? 마을에서 우리를 어떻게 대하는지 먼저 알고 싶어.",
     "이오: 마을의 요리사가 누구에게나 식사를 내어 준다는 이야기를 들었어. 나도 그 식탁에 앉아 보고 싶어.",
     "마을 요리사와 공존에 대해 대화", Skill::Root, Need::Kindness, 1, 15, 4, 33, 7, 3, 3, 3, 15, 3, {112, 184, 116}},
    {"살로", "불씨 도마뱀족", "범위 공격", "불씨 폭발", "사거리 3 / 대상 주변 1칸에 피해",
     "살로: 내 불을 보고 무기부터 떠올리는 녀석들이 많아. 나는 요리도 제법 잘하는데 말이야.",
     "살로: 여행자를 지킨 네 행동은 봤어. 네 곁이라면 내 불을 어디에 쓸지 스스로 정할 수 있겠지.",
     "첫 전투 승리", Skill::Flame, Need::Victory, 1, 17, 10, 31, 10, 2, 4, 2, 12, 4, {224, 133, 83}},
    {"세라", "비단 거미족", "지속 피해 · 원거리", "독실 매듭", "사거리 4 / 피해 + 독 3회",
     "세라: 구조용 그물을 짜는 중이야. 실에 바를 약초가 조금 부족하네. 날 도와줄 수 있겠어?",
     "세라: 고마워. 나를 보고 덫부터 떠올리지 않는 여행자는 드물어. 나도 네 길을 돕고 싶어.",
     "푸른 약초 1개 전달", Skill::Venom, Need::Herb, 2, 16, 3, 30, 8, 2, 4, 3, 12, 3, {183, 136, 191}},
    {"누아", "메아리 박쥐족", "흡수 · 기동", "공명 흡수", "사거리 3 / 피해를 주고 자신 회복",
     "누아: 바람길에서 편지를 잃어버렸어. 누군가 내 이름으로 답장을 보냈다는 게 처음이라, 꼭 찾고 싶어.",
     "누아: 편지를 찾아 줬구나! 친구에게 내가 직접 답장을 전하고 싶어. 북쪽으로 가는 길, 함께해도 될까?",
     "바람길의 떨어진 편지 발견", Skill::Drain, Need::Letter, 2, 18, 10, 28, 9, 1, 5, 2, 12, 3, {143, 146, 204}},
    {"유리", "등불 여우족", "공격 지원 · 원거리", "길동무의 등불", "주변 2칸 / 아군 공격 +3, 2턴",
     "유리: 인간도 몬스터도 같은 식탁에 앉는 곳을 찾고 있어. 이 마을은 어떨까?",
     "유리: 작은 식탁에서 시작해도 괜찮겠지. 네 여행에 빛을 보태고 싶어. 계약은 서로 지킬 약속으로 하자.",
     "마을 요리사와 공존에 대해 대화", Skill::Rally, Need::Kindness, 2, 5, 10, 30, 7, 2, 4, 3, 15, 3, {225, 177, 112}}
}};

enum class EventKind { Monster, Herb, Shrine, Letter, Elder, Exit, Archivist, Cook, Battle };
struct WorldEvent
{
    int map, x, y;
    EventKind kind;
    int value;
    const char* label;
};
inline const std::vector<WorldEvent> Events = [] {
    std::vector<WorldEvent> e;
    for (int i = 0; i < 10; ++i)
        e.push_back({Monsters[i].map, Monsters[i].x, Monsters[i].y, EventKind::Monster, i, Monsters[i].name});
    e.insert(e.end(), {
        {0, 3, 8, EventKind::Elder, 0, "숲의 어른"},
        {0, 11, 2, EventKind::Shrine, 6, "돌제단"},
        {0, 4, 6, EventKind::Herb, 0, "푸른 약초"},
        {0, 12, 10, EventKind::Herb, 1, "푸른 약초"},
        {0, 18, 3, EventKind::Herb, 2, "푸른 약초"},
        {0, 20, 7, EventKind::Exit, 1, "바람길로"},
        {1, 1, 7, EventKind::Exit, 0, "숲으로"},
        {1, 20, 7, EventKind::Exit, 2, "돌여울 마을로"},
        {1, 10, 7, EventKind::Battle, 0, "길목의 검문소"},
        {1, 16, 2, EventKind::Battle, 2, "제국 보급 창고"},
        {1, 7, 11, EventKind::Letter, 7, "떨어진 편지"},
        {1, 12, 3, EventKind::Herb, 3, "푸른 약초"},
        {1, 4, 10, EventKind::Herb, 4, "푸른 약초"},
        {2, 1, 7, EventKind::Exit, 1, "바람길로"},
        {2, 7, 4, EventKind::Archivist, 0, "기록원 이안"},
        {2, 12, 10, EventKind::Cook, 8, "요리사 마레"},
        {2, 15, 7, EventKind::Battle, 1, "압수 수레"},
        {2, 18, 4, EventKind::Herb, 5, "푸른 약초"}
    });
    return e;
}();
inline const std::array<const char*, 3> RegionNames{"고요나무 숲", "바람길", "돌여울 마을"};
inline const std::array<const char*, 3> BattleNames{"길을 막은 검문", "돌려받아야 할 식량", "닫힌 창고의 장부"};
inline const std::array<const char*, 6> Objectives{
    "바람길의 검문소를 조사하고 마을로 가는 길을 열자.",
    "돌여울 마을의 기록원 이안에게 어머니의 흔적을 묻자.",
    "마을 동쪽의 압수 수레에서 주민들의 식량을 되찾자.",
    "바람길 북동쪽 보급 창고에서 압수 장부를 확보하자.",
    "돌여울 마을의 기록원 이안에게 장부를 전달하자.",
    "1장 완료. 남은 동료를 만나거나 다른 편성을 시험해 보자."
};
}
