# 캐릭터 배치 시스템 코드 적용 프롬프트

## 목표

`resources/maps/world/character_placements.json`을 읽어 주인공 시작 위치와 현재 맵의 NPC를 배치한다. 리소스 파일과 이미지에는 손대지 말고 C++ 소스코드와 필요한 테스트만 수정한다.

## 현재 리소스 구조

- 프로젝트 리소스 루트: `resources/`
- 배치 파일: `resources/maps/world/character_placements.json`
- 캐릭터 인덱스: `resources/characters/character_info.json`
- 캐릭터 정의: `resources/characters/{characterId}/character.json`
- 게임 시작 설정: `src/game_config.json`
- 월드맵 파일명은 `y_x.map`이지만 JSON의 좌표는 항상 명시적인 `{ "x": ..., "y": ... }`를 사용한다.

## 배치 JSON 형식

- 최상위 키는 `chapter1`, `chapter2` 같은 `chapter{n}` 형식이다.
- `chapters` 래퍼와 `schemaVersion`은 사용하지 않는다.
- 선택할 키는 `src/game_config.json`의 `startMap.chapter` 값으로 `"chapter" + n`을 만들어 결정한다.
- `playerStart`에는 `position`과 `direction`만 있다.
- 주인공 `characterId`는 배치 파일에서 읽지 않고 `src/game_config.json`의 `character.id`를 사용한다.
- 시작 맵 좌표는 `src/game_config.json`의 `startMap.x`, `startMap.y`를 사용한다.
- `maps`의 각 원소는 `map` 좌표와 `actors` 배열을 가진다.
- 각 NPC는 `instanceId`, `characterId`, `position`, `direction`, `state`, `collision`, `enabled`, `interactionId`를 가진다.
- `cell`, `behavior`, `schemaVersion`, `chapters` 필드를 새로 추가하지 않는다.
- `position`은 픽셀이 아니라 해당 맵 내부의 0 기반 타일 좌표다.

## 구현 요구사항

1. 기존 `projectDirectory()`를 사용해 배치 파일의 절대 경로를 계산한다. 현재 리소스 루트가 `resources`라는 사실을 중복 하드코딩하지 않는다.
2. 기존 JSON 파싱 코드의 엄격한 필드 검증 방식과 오류 메시지 스타일을 따른다.
3. `chapter{n}`이 없거나 형식이 잘못된 경우 파일 경로와 필드 경로가 포함된 명확한 예외를 발생시킨다.
4. `instanceId`는 전체 배치 파일에서 유일해야 하며 빈 문자열을 허용하지 않는다.
5. `characterId`는 `CharacterManager`에 등록된 ID여야 한다.
6. `direction`은 `up`, `down`, `left`, `right` 중 하나이며, 해당 캐릭터의 요청된 `state/direction` 애니메이션이 실제로 존재해야 한다.
7. `position`이 맵 크기 안에 있고 이동 가능한 타일인지 검사한다.
8. 같은 맵에서 `enabled == true`이고 `collision == true`인 캐릭터끼리 같은 위치를 사용할 수 없게 한다.
9. 주인공 시작 위치 역시 맵 범위와 이동 가능 여부를 검사하며 충돌 NPC와 겹칠 수 없다.
10. `CharacterService::placePlayer`의 하드코딩된 `{2, 7}`을 제거하고 선택된 챕터의 `playerStart.position`과 `playerStart.direction`을 사용한다.
11. 주인공의 캐릭터 ID와 시작 맵은 계속 `GameSettings`/`game_config.json`에서 가져온다.
12. 현재 맵에 진입하면 그 맵의 `enabled` NPC만 `CharacterManager::appear`로 생성한다. 다른 맵으로 이동하면 이전 맵의 비플레이어 액터를 제거하고 새 맵의 NPC를 생성한다.
13. 같은 맵을 다시 로드해도 동일한 `instanceId`가 중복 생성되지 않도록 한다.
14. 런타임 액터에 배치 `instanceId`, `collision`, `interactionId`, 소속 chapter/map 좌표를 연결할 수 있는 메타데이터 구조를 추가한다.
15. 렌더링은 현재 맵에 속한 플레이어와 NPC만 대상으로 한다.
16. 플레이어 이동 시 `collision == true`인 NPC가 있는 위치로 진입하지 못하게 한다. 현재 단계에서는 NPC 이동과 `behavior`를 구현하지 않는다.
17. `enabled == false`인 NPC는 생성하거나 충돌 대상으로 사용하지 않는다.
18. `interactionId`는 보관만 하고 대화 시스템은 구현하지 않는다.
19. 배치 리소스는 읽기 전용 원본이다. 플레이 중 변경된 위치를 이 JSON에 다시 쓰지 않는다. 세이브 시스템 연동은 이번 작업 범위에서 제외한다.

## 현재 데이터로 확인할 결과

- Chapter 1 시작 맵: `(0, 0)`
- 주인공 시작 위치: `(2, 8)`
- 주인공 시작 방향: `right`
- NPC 인스턴스: `ch1_west_gate_keeper_0001`
- NPC 캐릭터: `00000002`
- NPC 위치: `(5, 7)`
- NPC 방향 및 상태: `down`, `idle`
- NPC 충돌: 활성화

## 테스트 요구사항

- 정상 파일 파싱과 `chapter1` 선택
- 주인공 ID가 배치 JSON이 아닌 `game_config.json`에서 오는지 확인
- 주인공 위치와 방향 적용 확인
- `0_0`에서 NPC 한 명이 정확한 위치에 한 번만 생성되는지 확인
- NPC 충돌 칸으로 플레이어가 이동하지 못하는지 확인
- 맵 전환 시 이전 NPC 제거 및 새 맵 NPC 로드 확인
- 잘못된 chapter 키, 중복 instanceId, 알 수 없는 characterId, 범위 밖 position, 막힌 타일, 중복 충돌 위치에 대한 실패 확인
- 기존 캐릭터 애니메이션, 맵 이동과 인접 맵 비동기 로딩 테스트가 계속 통과해야 한다.

## 완료 조건

- 빌드가 성공한다.
- 기존 테스트와 새 배치 테스트가 모두 통과한다.
- 코드에 `projects/sample` 경로나 하드코딩된 캐릭터 배치 좌표가 남지 않는다.
- 이미지, 맵, 캐릭터 JSON과 배치 JSON은 수정하지 않는다.
