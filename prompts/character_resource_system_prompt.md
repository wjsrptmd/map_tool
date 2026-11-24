# 캐릭터 리소스 시스템 구현 프롬프트

이 프로젝트의 C++ 코드에 캐릭터 리소스 로딩 구조를 추가해줘. 리소스 파일과 폴더는 이미 `projects/{project}/characters/` 아래에 준비되어 있으며 수정하지 않는다.

## 리소스 경로

- 색인: `projects/{project}/characters/character_info.json`
- 캐릭터 정의: 색인의 `definition`이 가리키는 JSON
- 현재 주인공 정의: `projects/sample/characters/00000001/character.json`
- 이미지 경로는 각 `character.json`이 있는 폴더를 기준으로 해석한다.

## character_info.json

최상위 키는 `globalConfig`, `characters`뿐이다.

`globalConfig` 필드:

- `frameWidth`, `frameHeight`: 양의 정수
- `defaultFps`: 0 이상의 정수
- `textureSmooth`: 불리언

`characters` 원소 필드:

- `id`: 숫자로만 된 문자열이며 중복 불가
- `name`: 비어 있지 않은 문자열
- `definition`: characters 폴더 내부의 상대 JSON 경로이며 파일이 존재해야 함

## character.json

필수 필드:

- `id`, `name`: 색인 값과 일치
- `defaultState`, `defaultDirection`: `animations` 안에 실제 조합이 존재해야 함
- `anchor`: 정수 또는 실수 두 개의 배열 `[x, y]`
- `collisionRadius`: 양수
- `animations`: 상태와 방향별 애니메이션 객체

각 애니메이션 필드:

- `image`: 캐릭터 정의 폴더 기준 상대 경로이며 파일이 존재해야 함
- `frames`: 양의 정수
- `fps`: 0 이상의 수
- `loop`: 불리언

스프라이트 시트는 프레임을 가로로 배치한다. 이미지 높이는 `frameHeight`, 너비는 `frameWidth * frames`와 같아야 한다. 단일 정지 이미지는 `frames: 1`, `fps: 0`을 사용한다.

## 로딩과 렌더링

1. 시작 시에는 `character_info.json`만 읽는다.
2. 캐릭터 정의와 텍스처는 해당 캐릭터가 필요할 때 로딩하고 캐시한다.
3. 상대 경로가 characters 폴더 밖으로 나가지 못하게 검사한다.
4. 현재 원형 캐릭터 표시를 기본 캐릭터 `00000001`의 `idle/down` 스프라이트로 교체한다.
5. `anchor`가 캐릭터의 현재 타일 중앙에 놓이도록 렌더링한다.
6. 텍스처 smoothing은 `globalConfig.textureSmooth`를 따른다.
7. 아직 존재하지 않는 방향이나 상태를 임의로 생성하지 않는다. 요청된 조합이 없으면 명확한 오류를 낸다.
8. 현재 이동, 카메라, 맵 전환 규칙은 변경하지 않는다.

## 검증

- 잘못된 최상위 키, 누락 필드, 중복 ID, 잘못된 수치, 폴더 밖 경로, 없는 파일을 거부한다.
- 기본 상태와 방향 이미지가 정상적으로 로딩되어야 한다.
- `--self-test`에서 캐릭터 색인, 기본 캐릭터 정의, 기본 애니메이션과 이미지 크기를 검사한다.
- 기존 맵 자체 검사도 계속 통과해야 한다.
