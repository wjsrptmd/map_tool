# NPC 자동 이동 시스템 코드 적용 프롬프트

## 목표

`character.json`의 선택적 `move` 필드를 읽어 현재 맵의 NPC가 스스로 타일 단위로 이동하게 한다. 기존 플레이어 입력, 캐릭터 배치, 맵 전환, 애니메이션 구조를 유지한다. 이미지와 리소스 JSON은 수정하지 않고 C++ 소스코드와 테스트만 수정한다.

## 현재 구조

- 게임 루프와 입력: `src/Game.cpp`
- 캐릭터 배치 및 이동 충돌: `src/service/CharacterService.*`
- 캐릭터 정의와 런타임 액터: `src/manager/CharacterManager.*`
- 캐릭터 상태와 애니메이션: `src/model/Character.*`
- 캐릭터 및 선택적 이동 정의: `resources/characters/{characterId}/character.json`
- 배치 정의: `resources/maps/world/character_placements.json`
- 필드 문서: `src/character_move_fields.txt`

`Game`은 입력과 프레임 시간을 조정하고, 실제 위치 변경과 충돌 판단은 서비스/매니저 계층에 위임하는 현재 방향을 유지한다. NPC AI와 JSON 파싱을 `Game` 클래스에 직접 구현하지 않는다.

## character.json의 move 필드 규격

### 공통

- `move` 필드가 없으면 해당 캐릭터는 자동 이동하지 않는다.
- 플레이어 액터는 `move` 필드가 있어도 자동 이동하지 않는다.
- `speed`는 필수 양수 실수이며 단위는 초당 타일 수다.
- 이동 간격은 `1.0f / speed`초다.
- `type`, `intervalMs`, `idleChance` 필드는 허용하지 않는다.
- `route`와 `radius` 중 정확히 하나만 있어야 한다.
- 알 수 없는 필드는 기존 JSON 로더와 동일하게 오류로 처리한다.

### 경로 이동

- 허용 필드: `speed`, `route`, `loop`
- `route`는 `{ "x": int, "y": int }` 객체 배열이다.
- 좌표는 배치 JSON의 최초 `position`을 기준으로 한 상대 타일 좌표다.
- route는 2개 이상의 원소를 가져야 한다.
- 첫 원소는 반드시 `(0, 0)`이어야 한다.
- 연속한 두 원소의 맨해튼 거리는 정확히 1이어야 한다.
- `loop`는 필수 boolean이다.
- `loop == true`이면 마지막 원소와 첫 원소의 맨해튼 거리도 정확히 1이어야 한다.
- `loop == false`이면 마지막 위치에 도착한 후 `idle`로 정지한다.

### 무작위 이동

- 허용 필드: `speed`, `radius`
- `radius`는 0 이상의 정수이며 최초 배치 위치 기준 최대 맨해튼 거리다.
- 매 이동 시점에 상하좌우 중 하나를 고른다.
- 후보가 radius를 벗어나거나 막혀 있으면 이동하지 않고 다음 이동 시점까지 기다린다.
- `radius == 0`은 유효하지만 실제로 이동하지 않는다.

## 리소스 파싱

1. `CharacterManager::readCharacter()`가 `character.json`을 읽을 때 기존 최상위 필드와 함께 선택적 `move` 필드를 파싱한다.
2. 현재 최상위 필드 검증은 고정된 필드 목록을 사용하므로 `move`를 허용하되 필수로 만들지 않는다. 필수 필드 검사와 선택 필드 검사를 구분한다.
3. `move`가 없으면 정상적인 정지 캐릭터로 처리한다.
4. `move`가 존재하지만 객체가 아니거나 내부 필드가 잘못된 경우 `character.json` 경로와 `move` 필드 위치가 포함된 예외를 발생시킨다.
5. 파싱된 이동 정의는 캐릭터 정의와 함께 캐시한다. 프레임마다 JSON을 다시 읽거나 파싱하지 않는다.
6. 이동 정의와 런타임 타이머를 구분한다. 정의는 캐릭터 ID별로 공유하고 타이머와 routeIndex는 NPC 인스턴스별로 관리한다.

추천 모델 예시:

```cpp
struct MovementDefinition
{
    float speed = 0.f;
    std::vector<Cell> route;
    std::optional<int> radius;
    bool loop = false;
};

struct NpcMovementState
{
    Cell origin;
    float elapsed = 0.f;
    float walkRemaining = 0.f;
    std::size_t routeIndex = 0;
    bool finished = false;
};
```

정확한 타입 배치는 현재 프로젝트 스타일에 맞게 조정해도 되지만 이동 정의를 `Game`에 보관하지 않는다.

## Game 루프 연결

현재 `Game::play`에서 `animation.restart().asSeconds()`를 바로 `characters_.advance()`에 넘긴다. delta time을 한 번만 계산하고 같은 값을 NPC 서비스와 애니메이션에 전달한다.

```cpp
const float deltaSeconds = animation.restart().asSeconds();
service_.advanceNpcs(deltaSeconds);
characters_.advance(deltaSeconds);
```

- 맵을 비동기로 로딩하는 동안에는 NPC 이동을 갱신하지 않는다.
- 로딩 분기에서 애니메이션 시계를 재시작하는 현재 동작을 유지해 로딩 후 큰 delta가 들어오지 않게 한다.
- NPC 이동으로 카메라를 이동하지 않는다.

## NPC 런타임 이동

1. 현재 chapter와 현재 map에 속한 `player == false`, `enabled` 상태의 NPC만 처리한다.
2. `move` 정의가 없는 캐릭터는 건너뛴다.
3. NPC별 elapsed에 delta time을 누적한다.
4. elapsed가 `1 / speed` 이상이면 한 칸 이동을 시도하고 해당 간격만큼 차감한다.
5. 한 프레임에 여러 칸을 순간 이동하지 않도록 프레임당 최대 한 번만 이동한다. 남은 elapsed는 유지하되 지나치게 큰 값은 합리적으로 제한한다.
6. 경로 이동은 현재 routeIndex의 다음 원소를 최초 배치 위치에 더해 목적지를 구한다.
7. 무작위 이동은 상하좌우 한 칸 중 하나를 선택하되 radius 안쪽만 허용한다.
8. 목적지는 현재 맵 범위 안이고 `MapManger::walkable()`이 true여야 한다.
9. 자동 이동 NPC는 인접 맵으로 넘어가지 않는다.
10. 목적지가 플레이어 또는 `collision == true`인 다른 NPC에게 점유된 경우 이동하지 않는다.
11. 여러 NPC가 같은 프레임에 같은 칸을 선택하지 않도록 해당 프레임의 목적지를 예약한다.
12. NPC끼리 서로 위치를 맞바꾸는 이동은 허용하지 않는다.
13. 이동에 성공하면 방향을 계산해 `body.walk(direction)`을 호출한 뒤 `CharacterManager::move()`로 위치를 변경한다.
14. 이동에 실패하면 routeIndex를 변경하지 않고 현재 방향의 `idle` 상태로 둔다.
15. 한 번 이동한 뒤 보행 애니메이션 한 주기 동안 `walk`를 유지한 다음 같은 방향의 `idle`로 전환한다. 보행 주기 길이는 해당 `walk/direction` 애니메이션의 `frames / fps`로 계산하고, fps가 0 이하인 잘못된 보행 애니메이션은 명확한 오류로 처리한다.

## 충돌 처리 변경

현재 `CharacterService::occupied(Cell)`은 플레이어 이동을 위해 NPC만 검사하고 플레이어를 제외한다. NPC 이동에서도 사용할 수 있도록 자기 자신을 제외하는 일반 형태로 변경한다.

```cpp
bool occupied(Cell position, int ignoredInstance = -1) const;
```

- 플레이어는 항상 점유 대상으로 포함한다.
- NPC는 `collision == true`인 경우만 점유 대상으로 포함한다.
- `ignoredInstance`와 같은 액터는 제외한다.
- 플레이어 이동 시에는 플레이어 인스턴스를 `ignoredInstance`로 전달한다.
- NPC 이동 시에는 해당 NPC 인스턴스를 전달한다.

## 맵 진입과 상태 수명

1. `CharacterService::enterMap()`이 NPC를 생성할 때 해당 캐릭터의 선택적 `move` 정의를 확인하고 인스턴스별 이동 상태를 준비한다.
2. `origin`은 배치 JSON의 최초 position이며 route 상대 좌표의 기준이다.
3. 이동 상태는 안정적인 `instanceId`를 키로 관리한다.
4. 다른 맵으로 나갔다 돌아와도 같은 실행 세션에서는 현재 위치, routeIndex와 finished 상태를 복구한다.
5. 화면 밖 다른 맵의 NPC는 갱신하지 않는다.
6. 이동 상태를 `character.json` 또는 `character_placements.json`에 다시 쓰지 않는다.
7. 세이브 파일 영속화는 이번 작업 범위에서 제외한다.

## 현재 서문지기 검증 데이터

- 캐릭터 ID: `00000002`
- 인스턴스 ID: `ch1_west_gate_keeper_0001`
- 최초 배치 위치: `(5, 7)`
- speed: `0.8` 타일/초
- 실제 경로: `(5,7) -> (6,7) -> (7,7) -> (6,7) -> (5,7) ...`
- 경로는 길을 막지 않는 풀 타일 안에서 반복한다.

## 테스트 요구사항

- character.json에 move가 없을 때 NPC가 정지 상태로 유지되는지 확인
- move가 객체가 아니거나 알 수 없는 필드를 포함할 때 실패하는지 확인
- speed가 0, 음수, 문자열 또는 누락된 경우 실패 확인
- type, intervalMs, idleChance와 기타 알 수 없는 필드 거부 확인
- route/radius 동시 사용 및 둘 다 누락된 경우 실패 확인
- route 첫 좌표, 인접성, loop 연결 검증
- radius 음수 거부 확인
- speed 0.8에서 약 1.25초마다 한 칸만 이동하는지 확인
- 서문지기가 `(5,7) -> (6,7) -> (7,7) -> (6,7)` 순서로 움직이는지 확인
- 목적지가 벽, 맵 밖, 플레이어 또는 충돌 NPC일 때 이동하지 않는지 확인
- 실패한 경로 이동이 routeIndex를 소비하지 않는지 확인
- 맵 로딩 중과 다른 맵의 NPC가 이동하지 않는지 확인
- 맵 재진입 시 같은 세션의 위치와 routeIndex 복구 확인
- 플레이어 입력, 플레이어 충돌, 배치 로딩, 맵 전환과 애니메이션 기존 테스트가 계속 통과하는지 확인

## 완료 조건

- 빌드와 모든 테스트가 성공한다.
- `Game`에는 delta time 전달 호출 외의 NPC 판단 로직을 넣지 않는다.
- `move` 정의는 character.json과 함께 캐릭터별로 한 번만 로드되고 캐시된다.
- 리소스 이미지, `character.json`, `character_placements.json`은 수정하지 않는다.
- 코드에 `projects/sample` 경로나 특정 NPC의 ID/좌표를 하드코딩하지 않는다.
